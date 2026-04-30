#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <memory>

struct ColorBarInfo {
    cv::Rect region;
    std::vector<cv::Vec3f> labColors; // Lab colors sampled from top to bottom
};

// Auto-detect the color bar region on the right side of the image.
ColorBarInfo detectColorBar(const cv::Mat& image) {
    int h = image.rows;
    int w = image.cols;

    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);

    int searchStart = static_cast<int>(w * 0.75);

    int rightBound = w - 1;
    for (int x = w - 1; x > searchStart; --x) {
        int nonBorderCount = 0;
        for (int y = h / 4; y < 3 * h / 4; y += 5) {
            cv::Vec3b px = image.at<cv::Vec3b>(y, x);
            int brightness = (px[0] + px[1] + px[2]) / 3;
            if (brightness > 15 && brightness < 245)
                ++nonBorderCount;
        }
        if (nonBorderCount > (3 * h / 4 - h / 4) / (5 * 3)) {
            rightBound = x;
            break;
        }
    }

    struct ColScore {
        int x;
        double gradientScore;
        double horizontalUniformity;
    };
    std::vector<ColScore> scores;

    for (int x = searchStart; x <= rightBound; ++x) {
        double totalGrad = 0.0;
        int gradCount = 0;
        for (int y = h / 6; y < 5 * h / 6 - 2; y += 2) {
            cv::Vec3f c1 = lab.at<cv::Vec3b>(y, x);
            cv::Vec3f c2 = lab.at<cv::Vec3b>(y + 2, x);
            double dL = c1[0] - c2[0];
            double da = c1[1] - c2[1];
            double db = c1[2] - c2[2];
            totalGrad += std::sqrt(dL * dL + da * da + db * db);
            ++gradCount;
        }
        double avgGrad = gradCount > 0 ? totalGrad / gradCount : 0;

        double horizVar = 0.0;
        int hCount = 0;
        for (int y = h / 4; y < 3 * h / 4; y += 10) {
            if (x > 0 && x < w - 1) {
                cv::Vec3f cl = lab.at<cv::Vec3b>(y, x - 1);
                cv::Vec3f cr = lab.at<cv::Vec3b>(y, x + 1);
                double dL = cl[0] - cr[0];
                double da = cl[1] - cr[1];
                double db = cl[2] - cr[2];
                horizVar += std::sqrt(dL * dL + da * da + db * db);
                ++hCount;
            }
        }
        double avgHoriz = hCount > 0 ? horizVar / hCount : 999;

        scores.push_back({x, avgGrad, avgHoriz});
    }

    int bestStart = -1, bestEnd = -1;
    double bestScore = -1;

    for (size_t i = 0; i < scores.size(); ++i) {
        if (scores[i].gradientScore > 1.0 && scores[i].horizontalUniformity < 20.0) {
            int start = scores[i].x;
            int end = start;
            for (size_t j = i + 1; j < scores.size(); ++j) {
                if (scores[j].gradientScore > 1.0 && scores[j].horizontalUniformity < 20.0) {
                    end = scores[j].x;
                } else {
                    break;
                }
            }
            int width = end - start + 1;
            if (width >= 3 && width <= w / 6) {
                double regionScore = 0;
                for (int x = start; x <= end; ++x) {
                    int idx = x - searchStart;
                    if (idx >= 0 && idx < (int)scores.size())
                        regionScore += scores[idx].gradientScore;
                }
                if (regionScore > bestScore) {
                    bestScore = regionScore;
                    bestStart = start;
                    bestEnd = end;
                }
            }
        }
    }

    int topY = h / 8, bottomY = 7 * h / 8;
    if (bestStart >= 0) {
        int midX = (bestStart + bestEnd) / 2;

        for (int y = h / 2; y > 0; --y) {
            cv::Vec3b px = image.at<cv::Vec3b>(y, midX);
            int brightness = (px[0] + px[1] + px[2]) / 3;
            if (brightness < 10 || brightness > 250) {
                topY = y + 1;
                break;
            }
        }
        for (int y = h / 2; y < h - 1; ++y) {
            cv::Vec3b px = image.at<cv::Vec3b>(y, midX);
            int brightness = (px[0] + px[1] + px[2]) / 3;
            if (brightness < 10 || brightness > 250) {
                bottomY = y - 1;
                break;
            }
        }
    }

    if (bestStart < 0) {
        std::cerr << "[Warning] Auto-detection of color bar failed, using default region (rightmost 5%)\n";
        bestStart = static_cast<int>(w * 0.93);
        bestEnd = static_cast<int>(w * 0.98);
        topY = static_cast<int>(h * 0.1);
        bottomY = static_cast<int>(h * 0.9);
    }

    topY = std::max(0, topY);
    bottomY = std::min(h - 1, bottomY);
    bestStart = std::max(0, bestStart);
    bestEnd = std::min(w - 1, bestEnd);

    ColorBarInfo info;
    info.region = cv::Rect(bestStart, topY, bestEnd - bestStart + 1, bottomY - topY + 1);

    int sampleCount = bottomY - topY + 1;
    info.labColors.resize(sampleCount);

    for (int y = topY; y <= bottomY; ++y) {
        cv::Vec3f avgColor(0, 0, 0);
        int count = 0;
        int margin = std::max(1, (bestEnd - bestStart + 1) / 4);
        for (int x = bestStart + margin; x <= bestEnd - margin; ++x) {
            cv::Vec3b c = lab.at<cv::Vec3b>(y, x);
            avgColor[0] += c[0];
            avgColor[1] += c[1];
            avgColor[2] += c[2];
            ++count;
        }
        if (count > 0) {
            avgColor[0] /= count;
            avgColor[1] /= count;
            avgColor[2] /= count;
        }
        info.labColors[y - topY] = avgColor;
    }

    return info;
}

// Extract a numeric value from OCR text using regex.
// Returns all candidate numbers found in the text, sorted by likelihood.
std::vector<double> extractNumbers(const std::string& text) {
    std::vector<double> results;

    std::string cleaned = text;
    // Common OCR misreads
    for (auto& c : cleaned) {
        if (c == 'O' || c == 'o') c = '0';
        if (c == 'l' || c == 'I') c = '1';
        if (c == 'S' || c == 's') c = '5';
        if (c == 'B') c = '8';
        if (c == 'G') c = '6';
        if (c == 'Z') c = '2';
        if (c == 'D') c = '0';
        if (c == 'q') c = '9';
    }

    std::regex numPattern(R"([-+]?\d+\.?\d*)");
    auto it = std::sregex_iterator(cleaned.begin(), cleaned.end(), numPattern);
    auto end = std::sregex_iterator();

    for (; it != end; ++it) {
        try {
            double val = std::stod(it->str());
            if (val > -200 && val < 10000) {
                results.push_back(val);
            }
        } catch (...) {}
    }
    return results;
}

bool extractNumber(const std::string& text, double& value) {
    auto nums = extractNumbers(text);
    if (!nums.empty()) {
        value = nums[0];
        return true;
    }
    return false;
}

// Use Tesseract OCR to recognize temperature text near the color bar.
// Searches above/below and to the right of the color bar for numeric labels.
bool ocrTemperatureLabels(const cv::Mat& image, const ColorBarInfo& colorBar,
                          double& maxTemp, double& minTemp) {
    int barX = colorBar.region.x;
    int barY = colorBar.region.y;
    int barW = colorBar.region.width;
    int barH = colorBar.region.height;
    int barRight = barX + barW;
    int barBottom = barY + barH;

    int imgW = image.cols;
    int imgH = image.rows;

    // Initialize Tesseract
    auto tess = std::make_unique<tesseract::TessBaseAPI>();
    // Suppress Tesseract debug output
    tess->SetVariable("debug_file", "/dev/null");

    if (tess->Init(nullptr, "eng", tesseract::OEM_LSTM_ONLY) != 0) {
        if (tess->Init(nullptr, "eng") != 0) {
            std::cerr << "[Error] Failed to initialize Tesseract OCR\n";
            return false;
        }
    }
    // Restrict to digits, decimal point, minus sign
    tess->SetVariable("tessedit_char_whitelist", "0123456789.-+");
    tess->SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    // Define candidate regions to search for temperature text.
    // Strategy: look at several positions near the color bar top/bottom:
    //   1. To the right of the color bar (most common layout)
    //   2. Above/below the color bar
    //   3. To the left of the color bar
    struct SearchRegion {
        cv::Rect roi;
        bool isMax; // true = looking for max temp, false = looking for min temp
        std::string label;
        int priority; // lower = more likely location
    };

    int textHeight = std::max(25, barH / 12);
    int textWidth = std::max(80, static_cast<int>(imgW * 0.15));

    std::vector<SearchRegion> regions;

    // Max temp candidates (near top of color bar)
    // Right of bar, aligned with top — multiple vertical offsets
    if (barRight + 2 < imgW) {
        int x1 = barRight + 2;
        int x2 = std::min(imgW, x1 + textWidth);
        // Slightly above bar top
        int y1 = std::max(0, barY - textHeight);
        int y2 = std::min(imgH, barY + textHeight / 2);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), true, "right-top-above", 1});
        // Aligned with bar top
        y1 = std::max(0, barY - 5);
        y2 = std::min(imgH, barY + textHeight);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), true, "right-top-aligned", 1});
    }
    // Above the color bar (wider region)
    {
        int x1 = std::max(0, barX - 20);
        int x2 = std::min(imgW, barRight + textWidth + 20);
        int y1 = std::max(0, barY - textHeight * 2);
        int y2 = barY;
        if (y2 > y1 + 5)
            regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), true, "above-bar", 2});
    }
    // Left of bar, aligned with top
    if (barX > 30) {
        int x1 = std::max(0, barX - textWidth - 5);
        int x2 = barX - 2;
        int y1 = std::max(0, barY - textHeight / 2);
        int y2 = std::min(imgH, barY + textHeight);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), true, "left-top", 3});
    }

    // Min temp candidates (near bottom of color bar)
    // Right of bar, aligned with bottom — multiple vertical offsets
    if (barRight + 2 < imgW) {
        int x1 = barRight + 2;
        int x2 = std::min(imgW, x1 + textWidth);
        // Slightly below bar bottom
        int y1 = std::max(0, barBottom - textHeight / 2);
        int y2 = std::min(imgH, barBottom + textHeight);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), false, "right-bottom-below", 1});
        // Aligned with bar bottom
        y1 = std::max(0, barBottom - textHeight);
        y2 = std::min(imgH, barBottom + 5);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), false, "right-bottom-aligned", 1});
    }
    // Below the color bar
    {
        int x1 = std::max(0, barX - 20);
        int x2 = std::min(imgW, barRight + textWidth + 20);
        int y1 = barBottom;
        int y2 = std::min(imgH, barBottom + textHeight * 2);
        if (y2 > y1 + 5)
            regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), false, "below-bar", 2});
    }
    // Left of bar, aligned with bottom
    if (barX > 30) {
        int x1 = std::max(0, barX - textWidth - 5);
        int x2 = barX - 2;
        int y1 = std::max(0, barBottom - textHeight);
        int y2 = std::min(imgH, barBottom + textHeight / 2);
        regions.push_back({cv::Rect(x1, y1, x2 - x1, y2 - y1), false, "left-bottom", 3});
    }

    // Collect all candidate detections
    struct OcrCandidate {
        double value;
        int confidence;
        bool isMax;
        std::string label;
        int priority; // lower = better position (right > above > left)
    };
    std::vector<OcrCandidate> candidates;

    int regionIdx = 0;
    for (const auto& sr : regions) {
        ++regionIdx;
        if (sr.roi.width <= 0 || sr.roi.height <= 0) continue;
        if (sr.roi.x < 0 || sr.roi.y < 0) continue;
        if (sr.roi.x + sr.roi.width > imgW || sr.roi.y + sr.roi.height > imgH) continue;

        cv::Mat roi = image(sr.roi);

        cv::Mat gray;
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);

        // Upscale small regions for better OCR accuracy
        int scale = 1;
        if (gray.rows < 30) scale = 4;
        else if (gray.rows < 50) scale = 3;
        else if (gray.rows < 80) scale = 2;

        if (scale > 1) {
            cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_CUBIC);
        }

        int priority = sr.priority;

        // Multiple binarization strategies
        std::vector<cv::Mat> binaries;

        // Otsu threshold and its inverse
        cv::Mat otsu;
        cv::threshold(gray, otsu, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        binaries.push_back(otsu);
        cv::Mat otsuInv;
        cv::bitwise_not(otsu, otsuInv);
        binaries.push_back(otsuInv);

        // Adaptive threshold
        if (gray.rows >= 15 && gray.cols >= 15) {
            cv::Mat adaptive;
            int blockSize = std::max(3, (gray.rows / 4) | 1); // ensure odd
            cv::adaptiveThreshold(gray, adaptive, 255,
                                  cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                  cv::THRESH_BINARY, blockSize, 5);
            binaries.push_back(adaptive);
            cv::Mat adaptiveInv;
            cv::bitwise_not(adaptive, adaptiveInv);
            binaries.push_back(adaptiveInv);
        }

        // Fixed threshold for white-on-black text
        cv::Mat fixedThresh;
        cv::threshold(gray, fixedThresh, 128, 255, cv::THRESH_BINARY);
        binaries.push_back(fixedThresh);
        cv::Mat fixedInv;
        cv::bitwise_not(fixedThresh, fixedInv);
        binaries.push_back(fixedInv);

        for (auto& processed : binaries) {
            // Morphological cleanup
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
            cv::Mat cleaned;
            cv::morphologyEx(processed, cleaned, cv::MORPH_CLOSE, kernel);

            // Add padding
            int pad = 15;
            cv::Mat padded;
            cv::copyMakeBorder(cleaned, padded, pad, pad, pad, pad,
                               cv::BORDER_CONSTANT, cv::Scalar(255));

            tess->SetImage(padded.data, padded.cols, padded.rows, 1, padded.step);
            tess->Recognize(0);

            const char* text = tess->GetUTF8Text();
            int confidence = tess->MeanTextConf();

            if (text && strlen(text) > 0 && confidence > 0) {
                std::string ocrText(text);
                auto nums = extractNumbers(ocrText);
                for (double value : nums) {
                    // Skip zero or very small values (likely noise)
                    if (std::abs(value) < 0.01) continue;
                    candidates.push_back({value, confidence, sr.isMax, sr.label, priority});
                }
            }
            delete[] text;
        }
    }

    tess->End();

    // Select best max/min from candidates using a scoring function:
    // - Higher confidence is better
    // - Lower priority (better positioned region) is better
    // - Non-zero values preferred
    auto score = [](const OcrCandidate& c) -> double {
        return c.confidence * 10.0 / c.priority;
    };

    bool foundMax = false, foundMin = false;
    double bestMaxScore = -1, bestMinScore = -1;

    for (const auto& c : candidates) {
        double s = score(c);
        if (c.isMax && s > bestMaxScore) {
            maxTemp = c.value;
            foundMax = true;
            bestMaxScore = s;
        } else if (!c.isMax && s > bestMinScore) {
            minTemp = c.value;
            foundMin = true;
            bestMinScore = s;
        }
    }

    if (foundMax) {
        std::cout << "  [OCR] Max temp: " << maxTemp << "\n";
    }
    if (foundMin) {
        std::cout << "  [OCR] Min temp: " << minTemp << "\n";
    }

    // Ensure max > min; swap if OCR got them reversed
    if (foundMax && foundMin && maxTemp < minTemp) {
        std::swap(maxTemp, minTemp);
        std::cout << "  [OCR] Swapped max/min (max was less than min)\n";
    }

    return foundMax && foundMin;
}

// Find the temperature for a given pixel color by matching against the color bar.
double findTemperature(const cv::Vec3f& pixelLab,
                       const std::vector<cv::Vec3f>& barColors,
                       double maxTemp, double minTemp) {
    double bestDist = std::numeric_limits<double>::max();
    int bestIdx = 0;

    for (int i = 0; i < (int)barColors.size(); ++i) {
        double dL = pixelLab[0] - barColors[i][0];
        double da = pixelLab[1] - barColors[i][1];
        double db = pixelLab[2] - barColors[i][2];
        double dist = dL * dL + da * da + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }

    double ratio = static_cast<double>(bestIdx) / std::max(1, (int)barColors.size() - 1);
    return maxTemp - ratio * (maxTemp - minTemp);
}

// Optimized version using a precomputed 3D lookup table in Lab space
class TemperatureLUT {
public:
    static const int BINS = 32;

    TemperatureLUT(const std::vector<cv::Vec3f>& barColors, double maxTemp, double minTemp)
        : maxTemp_(maxTemp), minTemp_(minTemp), barColors_(barColors) {
        lut_.resize(BINS * BINS * BINS, -1.0);
        buildLUT();
    }

    double lookup(const cv::Vec3f& labPixel) const {
        int bL = std::min(BINS - 1, std::max(0, static_cast<int>(labPixel[0] * BINS / 256.0)));
        int ba = std::min(BINS - 1, std::max(0, static_cast<int>(labPixel[1] * BINS / 256.0)));
        int bb = std::min(BINS - 1, std::max(0, static_cast<int>(labPixel[2] * BINS / 256.0)));
        int idx = bL * BINS * BINS + ba * BINS + bb;
        if (lut_[idx] >= 0) return lut_[idx];
        return findTemperature(labPixel, barColors_, maxTemp_, minTemp_);
    }

private:
    void buildLUT() {
        for (int bL = 0; bL < BINS; ++bL) {
            for (int ba = 0; ba < BINS; ++ba) {
                for (int bb = 0; bb < BINS; ++bb) {
                    float L = (bL + 0.5f) * 256.0f / BINS;
                    float a = (ba + 0.5f) * 256.0f / BINS;
                    float b = (bb + 0.5f) * 256.0f / BINS;
                    cv::Vec3f color(L, a, b);
                    int idx = bL * BINS * BINS + ba * BINS + bb;
                    lut_[idx] = findTemperature(color, barColors_, maxTemp_, minTemp_);
                }
            }
        }
    }

    double maxTemp_, minTemp_;
    std::vector<cv::Vec3f> barColors_;
    std::vector<double> lut_;
};

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName
              << " <image_path> [max_temp min_temp] [options]\n\n"
              << "Arguments:\n"
              << "  image_path     Path to the infrared pseudo-color image\n"
              << "  max_temp       Maximum temperature (top of color bar) [auto-detected via OCR if omitted]\n"
              << "  min_temp       Minimum temperature (bottom of color bar) [auto-detected via OCR if omitted]\n\n"
              << "Options:\n"
              << "  --bar-x1 N     Color bar left x coordinate (manual override)\n"
              << "  --bar-x2 N     Color bar right x coordinate\n"
              << "  --bar-y1 N     Color bar top y coordinate\n"
              << "  --bar-y2 N     Color bar bottom y coordinate\n"
              << "  --output-csv FILE   Output temperature matrix as CSV\n"
              << "  --output-img FILE   Output temperature heatmap as image\n"
              << "  --show              Display result in a window\n"
              << "  --exclude-bar       Exclude the color bar region from output\n"
              << "  --no-ocr            Disable OCR auto-detection (requires manual temp input)\n\n"
              << "Examples:\n"
              << "  # Fully automatic (OCR detects temperature range):\n"
              << "  " << programName << " thermal.jpg --output-csv temp.csv\n\n"
              << "  # Manual temperature input:\n"
              << "  " << programName << " thermal.jpg 45.2 22.1 --output-csv temp.csv\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string imagePath = argv[1];
    double maxTemp = 0, minTemp = 0;
    bool tempProvided = false;
    bool noOcr = false;

    // Determine if second argument is a number (temperature) or an option.
    // A numeric argument starts with a digit, or '-'/'+' followed by a digit.
    int optStart = 2;
    if (argc >= 4) {
        std::string arg2 = argv[2];
        bool isNumeric = false;
        if (!arg2.empty()) {
            if (std::isdigit(static_cast<unsigned char>(arg2[0]))) {
                isNumeric = true;
            } else if ((arg2[0] == '-' || arg2[0] == '+') && arg2.size() > 1
                       && std::isdigit(static_cast<unsigned char>(arg2[1]))) {
                isNumeric = true;
            }
        }
        if (isNumeric) {
            maxTemp = std::atof(argv[2]);
            minTemp = std::atof(argv[3]);
            tempProvided = true;
            optStart = 4;
        }
    }

    // Parse optional arguments
    int barX1 = -1, barX2 = -1, barY1 = -1, barY2 = -1;
    std::string outputCsv, outputImg;
    bool showWindow = false;
    bool excludeBar = false;

    for (int i = optStart; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bar-x1" && i + 1 < argc) barX1 = std::atoi(argv[++i]);
        else if (arg == "--bar-x2" && i + 1 < argc) barX2 = std::atoi(argv[++i]);
        else if (arg == "--bar-y1" && i + 1 < argc) barY1 = std::atoi(argv[++i]);
        else if (arg == "--bar-y2" && i + 1 < argc) barY2 = std::atoi(argv[++i]);
        else if (arg == "--output-csv" && i + 1 < argc) outputCsv = argv[++i];
        else if (arg == "--output-img" && i + 1 < argc) outputImg = argv[++i];
        else if (arg == "--show") showWindow = true;
        else if (arg == "--exclude-bar") excludeBar = true;
        else if (arg == "--no-ocr") noOcr = true;
    }

    // Load image
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "[Error] Cannot load image: " << imagePath << "\n";
        return 1;
    }

    std::cout << "Image loaded: " << image.cols << " x " << image.rows << "\n";

    // Detect or use specified color bar region
    ColorBarInfo colorBar;
    if (barX1 >= 0 && barX2 >= 0 && barY1 >= 0 && barY2 >= 0) {
        std::cout << "Using manually specified color bar region\n";
        colorBar.region = cv::Rect(barX1, barY1, barX2 - barX1 + 1, barY2 - barY1 + 1);

        cv::Mat lab;
        cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);

        int sampleCount = barY2 - barY1 + 1;
        colorBar.labColors.resize(sampleCount);
        for (int y = barY1; y <= barY2; ++y) {
            cv::Vec3f avgColor(0, 0, 0);
            int count = 0;
            for (int x = barX1; x <= barX2; ++x) {
                cv::Vec3b c = lab.at<cv::Vec3b>(y, x);
                avgColor[0] += c[0];
                avgColor[1] += c[1];
                avgColor[2] += c[2];
                ++count;
            }
            if (count > 0) {
                avgColor /= count;
            }
            colorBar.labColors[y - barY1] = avgColor;
        }
    } else {
        std::cout << "Auto-detecting color bar...\n";
        colorBar = detectColorBar(image);
    }

    std::cout << "Color bar region: x=[" << colorBar.region.x << ", "
              << colorBar.region.x + colorBar.region.width - 1 << "], y=["
              << colorBar.region.y << ", "
              << colorBar.region.y + colorBar.region.height - 1 << "]\n";
    std::cout << "Color bar samples: " << colorBar.labColors.size() << "\n";

    // Auto-detect temperature range via OCR if not provided
    if (!tempProvided) {
        if (noOcr) {
            std::cerr << "[Error] Temperature range not provided and OCR is disabled.\n"
                      << "  Please provide max_temp and min_temp as arguments, or remove --no-ocr.\n";
            return 1;
        }

        std::cout << "Auto-detecting temperature range via OCR...\n";
        bool ocrSuccess = ocrTemperatureLabels(image, colorBar, maxTemp, minTemp);
        if (!ocrSuccess) {
            std::cerr << "[Error] OCR failed to detect temperature labels.\n"
                      << "  Please provide max_temp and min_temp as command-line arguments.\n"
                      << "  Example: " << argv[0] << " " << imagePath << " 45.2 22.1\n";
            return 1;
        }
        std::cout << "OCR detected temperature range: " << minTemp << " ~ " << maxTemp << "\n";
    } else {
        std::cout << "Temperature range (user-provided): " << minTemp << " ~ " << maxTemp << "\n";
    }

    if (maxTemp <= minTemp) {
        std::cerr << "[Error] max_temp (" << maxTemp << ") must be greater than min_temp ("
                  << minTemp << ")\n";
        return 1;
    }

    // Convert the full image to Lab color space
    cv::Mat labImage;
    cv::cvtColor(image, labImage, cv::COLOR_BGR2Lab);

    // Build the lookup table for faster processing
    std::cout << "Building temperature lookup table...\n";
    TemperatureLUT lut(colorBar.labColors, maxTemp, minTemp);

    // Determine the output region
    int outX1 = 0, outY1 = 0;
    int outX2 = image.cols - 1, outY2 = image.rows - 1;
    if (excludeBar) {
        outX2 = std::min(outX2, colorBar.region.x - 1);
    }
    int outW = outX2 - outX1 + 1;
    int outH = outY2 - outY1 + 1;

    // Compute temperature for each pixel
    std::cout << "Computing temperature map (" << outW << " x " << outH << ")...\n";
    cv::Mat tempMap(outH, outW, CV_64F);

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            cv::Vec3b labPx = labImage.at<cv::Vec3b>(y + outY1, x + outX1);
            cv::Vec3f labF(labPx[0], labPx[1], labPx[2]);
            double temp = lut.lookup(labF);
            tempMap.at<double>(y, x) = temp;
            globalMin = std::min(globalMin, temp);
            globalMax = std::max(globalMax, temp);
        }
        if (y % 100 == 0) {
            std::cout << "\r  Progress: " << (y * 100 / outH) << "%" << std::flush;
        }
    }
    std::cout << "\r  Progress: 100%\n";

    std::cout << "Computed temperature range: " << globalMin << " ~ " << globalMax << "\n";

    // Output CSV
    if (!outputCsv.empty()) {
        std::cout << "Writing CSV to " << outputCsv << "...\n";
        std::ofstream ofs(outputCsv);
        if (!ofs.is_open()) {
            std::cerr << "[Error] Cannot open output file: " << outputCsv << "\n";
            return 1;
        }
        ofs << std::fixed;
        ofs.precision(2);
        for (int y = 0; y < outH; ++y) {
            for (int x = 0; x < outW; ++x) {
                if (x > 0) ofs << ",";
                ofs << tempMap.at<double>(y, x);
            }
            ofs << "\n";
        }
        ofs.close();
        std::cout << "CSV written successfully.\n";
    }

    // Output temperature heatmap image
    if (!outputImg.empty() || showWindow) {
        cv::Mat normalized;
        tempMap.convertTo(normalized, CV_8U, 255.0 / (maxTemp - minTemp),
                          -255.0 * minTemp / (maxTemp - minTemp));

        cv::Mat heatmap;
        cv::applyColorMap(normalized, heatmap, cv::COLORMAP_JET);

        int barWidth = 40;
        int padding = 10;
        cv::Mat result(heatmap.rows, heatmap.cols + barWidth + padding * 2 + 60, CV_8UC3,
                       cv::Scalar(0, 0, 0));
        heatmap.copyTo(result(cv::Rect(0, 0, heatmap.cols, heatmap.rows)));

        for (int y = 0; y < heatmap.rows; ++y) {
            double temp = maxTemp - (double)y / (heatmap.rows - 1) * (maxTemp - minTemp);
            uchar val = static_cast<uchar>(255.0 * (temp - minTemp) / (maxTemp - minTemp));
            cv::Mat valMat(1, 1, CV_8U, val);
            cv::Mat colorMat;
            cv::applyColorMap(valMat, colorMat, cv::COLORMAP_JET);
            cv::Vec3b color = colorMat.at<cv::Vec3b>(0, 0);
            cv::line(result,
                     cv::Point(heatmap.cols + padding, y),
                     cv::Point(heatmap.cols + padding + barWidth, y),
                     cv::Scalar(color[0], color[1], color[2]), 1);
        }

        auto addLabel = [&](int y, double temp) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", temp);
            cv::putText(result, buf,
                        cv::Point(heatmap.cols + padding + barWidth + 5, y + 5),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4,
                        cv::Scalar(255, 255, 255), 1);
        };

        int numLabels = 5;
        for (int i = 0; i <= numLabels; ++i) {
            int y = i * (heatmap.rows - 1) / numLabels;
            double temp = maxTemp - (double)i / numLabels * (maxTemp - minTemp);
            addLabel(y, temp);
        }

        if (!outputImg.empty()) {
            std::cout << "Writing heatmap to " << outputImg << "...\n";
            cv::imwrite(outputImg, result);
            std::cout << "Heatmap written successfully.\n";
        }

        if (showWindow) {
            cv::imshow("Temperature Map", result);
            std::cout << "Press any key to close the window...\n";
            cv::waitKey(0);
        }
    }

    // Print a summary with some sample points
    std::cout << "\n=== Temperature Samples ===\n";
    int samplePoints[][2] = {
        {outW / 4, outH / 4},
        {outW / 2, outH / 4},
        {3 * outW / 4, outH / 4},
        {outW / 4, outH / 2},
        {outW / 2, outH / 2},
        {3 * outW / 4, outH / 2},
        {outW / 4, 3 * outH / 4},
        {outW / 2, 3 * outH / 4},
        {3 * outW / 4, 3 * outH / 4},
    };
    for (auto& pt : samplePoints) {
        std::cout << "  Pixel (" << pt[0] << ", " << pt[1] << "): "
                  << tempMap.at<double>(pt[1], pt[0]) << " deg\n";
    }

    if (outputCsv.empty() && outputImg.empty() && !showWindow) {
        std::cout << "\nHint: Use --output-csv or --output-img to save results.\n";
    }

    return 0;
}
