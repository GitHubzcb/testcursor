#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>

struct ColorBarInfo {
    cv::Rect region;
    std::vector<cv::Vec3f> labColors; // Lab colors sampled from top to bottom
};

// Auto-detect the color bar region on the right side of the image.
// Strategy: scan from the right edge inward, looking for a narrow vertical
// strip that exhibits a smooth color gradient from top to bottom.
ColorBarInfo detectColorBar(const cv::Mat& image) {
    int h = image.rows;
    int w = image.cols;

    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);

    // The color bar is typically in the rightmost 15% of the image.
    // We look for a vertical region where each column has high color variance
    // from top to bottom (gradient) but low variance across a narrow horizontal band.
    int searchStart = static_cast<int>(w * 0.75);

    // Find the right boundary: skip pure black/white border pixels
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

    // Score each column by vertical color gradient magnitude
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

    // Find the region with high gradient and low horizontal variance
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

    // Determine vertical extent of the color bar
    int topY = h / 8, bottomY = 7 * h / 8;
    if (bestStart >= 0) {
        int midX = (bestStart + bestEnd) / 2;

        // Search upward for the start of the gradient
        for (int y = h / 2; y > 0; --y) {
            cv::Vec3b px = image.at<cv::Vec3b>(y, midX);
            int brightness = (px[0] + px[1] + px[2]) / 3;
            if (brightness < 10 || brightness > 250) {
                topY = y + 1;
                break;
            }
        }
        // Search downward for the end
        for (int y = h / 2; y < h - 1; ++y) {
            cv::Vec3b px = image.at<cv::Vec3b>(y, midX);
            int brightness = (px[0] + px[1] + px[2]) / 3;
            if (brightness < 10 || brightness > 250) {
                bottomY = y - 1;
                break;
            }
        }
    }

    // Fallback if detection failed: use rightmost 5% of image
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

    // Sample colors along the color bar (top to bottom)
    int sampleCount = bottomY - topY + 1;
    info.labColors.resize(sampleCount);

    for (int y = topY; y <= bottomY; ++y) {
        cv::Vec3f avgColor(0, 0, 0);
        int count = 0;
        // Average across the width of the color bar for noise reduction
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

// Find the temperature for a given pixel color by matching against the color bar.
// Returns a temperature linearly interpolated between maxTemp (top) and minTemp (bottom).
double findTemperature(const cv::Vec3f& pixelLab,
                       const std::vector<cv::Vec3f>& barColors,
                       double maxTemp, double minTemp) {
    double bestDist = std::numeric_limits<double>::max();
    int bestIdx = 0;

    for (int i = 0; i < (int)barColors.size(); ++i) {
        double dL = pixelLab[0] - barColors[i][0];
        double da = pixelLab[1] - barColors[i][1];
        double db = pixelLab[2] - barColors[i][2];
        // CIE76 Delta-E
        double dist = dL * dL + da * da + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }

    // Linear interpolation: top (index 0) -> maxTemp, bottom (last index) -> minTemp
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
              << " <image_path> <max_temp> <min_temp> [options]\n\n"
              << "Required arguments:\n"
              << "  image_path     Path to the infrared pseudo-color image\n"
              << "  max_temp       Maximum temperature (top of color bar)\n"
              << "  min_temp       Minimum temperature (bottom of color bar)\n\n"
              << "Options:\n"
              << "  --bar-x1 N     Color bar left x coordinate (manual override)\n"
              << "  --bar-x2 N     Color bar right x coordinate\n"
              << "  --bar-y1 N     Color bar top y coordinate\n"
              << "  --bar-y2 N     Color bar bottom y coordinate\n"
              << "  --output-csv FILE   Output temperature matrix as CSV\n"
              << "  --output-img FILE   Output temperature heatmap as image\n"
              << "  --show              Display result in a window\n"
              << "  --exclude-bar       Exclude the color bar region from output\n\n"
              << "Example:\n"
              << "  " << programName << " thermal.jpg 45.2 22.1 --output-csv temp.csv --output-img result.png\n";
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    std::string imagePath = argv[1];
    double maxTemp = std::atof(argv[2]);
    double minTemp = std::atof(argv[3]);

    // Parse optional arguments
    int barX1 = -1, barX2 = -1, barY1 = -1, barY2 = -1;
    std::string outputCsv, outputImg;
    bool showWindow = false;
    bool excludeBar = false;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bar-x1" && i + 1 < argc) barX1 = std::atoi(argv[++i]);
        else if (arg == "--bar-x2" && i + 1 < argc) barX2 = std::atoi(argv[++i]);
        else if (arg == "--bar-y1" && i + 1 < argc) barY1 = std::atoi(argv[++i]);
        else if (arg == "--bar-y2" && i + 1 < argc) barY2 = std::atoi(argv[++i]);
        else if (arg == "--output-csv" && i + 1 < argc) outputCsv = argv[++i];
        else if (arg == "--output-img" && i + 1 < argc) outputImg = argv[++i];
        else if (arg == "--show") showWindow = true;
        else if (arg == "--exclude-bar") excludeBar = true;
    }

    if (maxTemp <= minTemp) {
        std::cerr << "[Error] max_temp must be greater than min_temp\n";
        return 1;
    }

    // Load image
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "[Error] Cannot load image: " << imagePath << "\n";
        return 1;
    }

    std::cout << "Image loaded: " << image.cols << " x " << image.rows << "\n";
    std::cout << "Temperature range: " << minTemp << " ~ " << maxTemp << "\n";

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

        // Add temperature scale text
        int barWidth = 40;
        int padding = 10;
        cv::Mat result(heatmap.rows, heatmap.cols + barWidth + padding * 2 + 60, CV_8UC3,
                       cv::Scalar(0, 0, 0));
        heatmap.copyTo(result(cv::Rect(0, 0, heatmap.cols, heatmap.rows)));

        // Draw a color bar on the result
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

        // Add temperature labels
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
