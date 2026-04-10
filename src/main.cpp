#include <opencv2/opencv.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

enum class Polarity {
  Hot,
  Cold,
  Auto
};

struct Options {
  std::string input_path;
  int camera_index = 0;
  int min_area = 500;
  double max_area_ratio = 0.35;
  bool show_mask = false;
  bool display = true;
  Polarity polarity = Polarity::Auto;
};

static void PrintUsage(const char* argv0) {
  std::cout
      << "远红外人体检测 (OpenCV)\n"
      << "用法:\n"
      << "  " << argv0
      << " [--input <video_path>] [--camera <index>] [--min-area <pixels>]\n"
      << "      [--max-area-ratio <0-1>] [--polarity <hot|cold|auto>] [--show-mask]\n"
      << "      [--no-display]\n\n"
      << "说明:\n"
      << "  --input            输入视频路径；若不提供则使用摄像头。\n"
      << "  --camera           摄像头索引（默认 0）。\n"
      << "  --min-area         最小候选人体面积，像素单位（默认 500）。\n"
      << "  --max-area-ratio   最大候选面积占整帧比例（默认 0.35）。\n"
      << "  --polarity         热像极性：hot(人更亮), cold(人更暗), auto(自动)。\n"
      << "  --show-mask        显示二值掩码窗口。\n"
      << "  --no-display       不显示窗口（仅处理流程）。\n";
}

static bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) {
      options->input_path = argv[++i];
    } else if (arg == "--camera" && i + 1 < argc) {
      options->camera_index = std::stoi(argv[++i]);
    } else if (arg == "--min-area" && i + 1 < argc) {
      options->min_area = std::stoi(argv[++i]);
    } else if (arg == "--max-area-ratio" && i + 1 < argc) {
      options->max_area_ratio = std::stod(argv[++i]);
    } else if (arg == "--show-mask") {
      options->show_mask = true;
    } else if (arg == "--no-display") {
      options->display = false;
    } else if (arg == "--polarity" && i + 1 < argc) {
      const std::string mode = argv[++i];
      if (mode == "hot") {
        options->polarity = Polarity::Hot;
      } else if (mode == "cold") {
        options->polarity = Polarity::Cold;
      } else if (mode == "auto") {
        options->polarity = Polarity::Auto;
      } else {
        std::cerr << "未知 polarity: " << mode << "\n";
        return false;
      }
    } else if (arg == "--help" || arg == "-h") {
      return false;
    } else {
      std::cerr << "未知参数: " << arg << "\n";
      return false;
    }
  }

  if (options->min_area <= 0) {
    std::cerr << "--min-area 必须 > 0\n";
    return false;
  }
  if (options->max_area_ratio <= 0.0 || options->max_area_ratio > 1.0) {
    std::cerr << "--max-area-ratio 必须在 (0, 1] 范围\n";
    return false;
  }
  return true;
}

static cv::Mat BuildMaskFromPolarity(const cv::Mat& gray, Polarity polarity) {
  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

  cv::Mat binary;
  const int threshold_type = (polarity == Polarity::Hot) ? cv::THRESH_BINARY : cv::THRESH_BINARY_INV;
  cv::threshold(blurred, binary, 0.0, 255.0, threshold_type | cv::THRESH_OTSU);

  cv::Mat opened;
  cv::morphologyEx(
      binary,
      opened,
      cv::MORPH_OPEN,
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));
  cv::Mat cleaned;
  cv::morphologyEx(
      opened,
      cleaned,
      cv::MORPH_CLOSE,
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));

  return cleaned;
}

static double ScoreMaskQuality(const cv::Mat& mask) {
  const double frame_area = static_cast<double>(mask.rows * mask.cols);
  const double foreground_ratio = static_cast<double>(cv::countNonZero(mask)) / frame_area;

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  int valid_count = 0;
  double valid_area = 0.0;
  for (const auto& contour : contours) {
    const double area = cv::contourArea(contour);
    if (area > 120.0 && area < frame_area * 0.5) {
      ++valid_count;
      valid_area += area;
    }
  }

  double score = 0.0;
  score += std::min(valid_count, 8) * 0.2;
  score += std::min(valid_area / frame_area, 0.25) * 4.0;
  score -= std::abs(foreground_ratio - 0.12) * 2.0;
  if (foreground_ratio < 0.003 || foreground_ratio > 0.6) {
    score -= 2.0;
  }
  return score;
}

static cv::Mat BuildThermalMask(const cv::Mat& gray, Polarity polarity) {
  if (polarity == Polarity::Hot || polarity == Polarity::Cold) {
    return BuildMaskFromPolarity(gray, polarity);
  }

  cv::Mat hot = BuildMaskFromPolarity(gray, Polarity::Hot);
  cv::Mat cold = BuildMaskFromPolarity(gray, Polarity::Cold);

  return (ScoreMaskQuality(hot) >= ScoreMaskQuality(cold)) ? hot : cold;
}

int main(int argc, char** argv) {
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage(argv[0]);
    return 1;
  }

  cv::VideoCapture cap;
  if (!options.input_path.empty()) {
    cap.open(options.input_path);
  } else {
    cap.open(options.camera_index);
  }

  if (!cap.isOpened()) {
    std::cerr << "无法打开输入源: "
              << (options.input_path.empty() ? ("camera index " + std::to_string(options.camera_index))
                                             : options.input_path)
              << "\n";
    return 2;
  }

  const auto start_time = std::chrono::steady_clock::now();
  int frame_count = 0;
  cv::Mat frame;
  while (cap.read(frame)) {
    ++frame_count;
    if (frame.empty()) {
      continue;
    }

    cv::Mat gray;
    if (frame.channels() == 3) {
      cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
      cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
      gray = frame.clone();
    }

    cv::Mat mask = BuildThermalMask(gray, options.polarity);

    const double frame_area = static_cast<double>(frame.rows * frame.cols);
    const double max_area = frame_area * options.max_area_ratio;
    const int min_height = static_cast<int>(frame.rows * 0.08);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int people_count = 0;
    for (const auto& contour : contours) {
      const double area = cv::contourArea(contour);
      if (area < static_cast<double>(options.min_area) || area > max_area) {
        continue;
      }

      const cv::Rect bbox = cv::boundingRect(contour);
      if (bbox.height < min_height || bbox.width <= 0) {
        continue;
      }

      const double aspect = static_cast<double>(bbox.width) / bbox.height;
      if (aspect < 0.2 || aspect > 1.2) {
        continue;
      }

      std::vector<cv::Point> hull;
      cv::convexHull(contour, hull);
      const double hull_area = std::max(1.0, cv::contourArea(hull));
      const double solidity = area / hull_area;
      if (solidity < 0.35) {
        continue;
      }

      cv::rectangle(frame, bbox, cv::Scalar(0, 255, 0), 2);
      cv::putText(
          frame,
          "Person",
          cv::Point(bbox.x, std::max(0, bbox.y - 6)),
          cv::FONT_HERSHEY_SIMPLEX,
          0.55,
          cv::Scalar(0, 255, 0),
          2);
      ++people_count;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time).count();
    const double fps = (elapsed > 1e-6) ? static_cast<double>(frame_count) / elapsed : 0.0;

    cv::putText(
        frame,
        "Detections: " + std::to_string(people_count) + " | FPS: " + cv::format("%.1f", fps),
        cv::Point(15, 30),
        cv::FONT_HERSHEY_SIMPLEX,
        0.75,
        cv::Scalar(0, 255, 255),
        2);

    if (options.display) {
      cv::imshow("IR Human Detection", frame);
      if (options.show_mask) {
        cv::imshow("Binary Mask", mask);
      }

      const int key = cv::waitKey(1);
      if (key == 27 || key == 'q' || key == 'Q') {
        break;
      }
    }
  }

  return 0;
}
