#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <memory>

#ifdef USE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

struct ColorBarInfo {
    cv::Rect region;
    std::vector<cv::Vec3f> labColors; // Lab colors sampled from top to bottom
};

// ============================================================================
// Built-in digit recognizer (no external dependency, only OpenCV required)
// Primary: Template matching against multiple built-in font templates.
// Fallback: Structural feature classification.
// ============================================================================

class BuiltinDigitRecognizer {
private:
    // Normalized digit templates (generated once, cached as static)
    static const int TMPL_H = 32;
    static const int TMPL_W = 20;

    struct TemplateSet {
        cv::Mat templates[10]; // digits 0-9, each TMPL_H x TMPL_W, CV_8U
        bool initialized = false;
    };

    // Generate templates using OpenCV built-in fonts
    static std::vector<TemplateSet>& getTemplateSets() {
        static std::vector<TemplateSet> sets;
        if (!sets.empty()) return sets;

        // Use multiple OpenCV font faces for broader coverage
        int fonts[] = {
            cv::FONT_HERSHEY_SIMPLEX,
            cv::FONT_HERSHEY_DUPLEX,
            cv::FONT_HERSHEY_COMPLEX,
            cv::FONT_HERSHEY_TRIPLEX,
            cv::FONT_HERSHEY_PLAIN,
        };
        double scales[] = {1.5, 2.0, 1.0};
        int thicknesses[] = {2, 3, 1};

        for (int font : fonts) {
            for (double scale : scales) {
                for (int thick : thicknesses) {
                    TemplateSet ts;
                    bool valid = true;
                    for (int d = 0; d <= 9; ++d) {
                        std::string ch(1, '0' + d);
                        int baseline = 0;
                        cv::Size textSize = cv::getTextSize(ch, font, scale, thick, &baseline);

                        if (textSize.width < 3 || textSize.height < 5) {
                            valid = false;
                            break;
                        }

                        // Render on a tight canvas
                        int canvasH = textSize.height + baseline + 6;
                        int canvasW = textSize.width + 6;
                        cv::Mat canvas(canvasH, canvasW, CV_8U, cv::Scalar(0));
                        cv::putText(canvas, ch, cv::Point(3, textSize.height + 3),
                                    font, scale, cv::Scalar(255), thick);

                        // Crop to tight bounding box
                        std::vector<cv::Point> nonzero;
                        cv::findNonZero(canvas, nonzero);
                        if (nonzero.empty()) { valid = false; break; }
                        cv::Rect bbox = cv::boundingRect(nonzero);
                        cv::Mat cropped = canvas(bbox);

                        // Resize to standard template size
                        cv::Mat resized;
                        cv::resize(cropped, resized, cv::Size(TMPL_W, TMPL_H), 0, 0, cv::INTER_AREA);
                        cv::threshold(resized, ts.templates[d], 64, 255, cv::THRESH_BINARY);
                    }
                    if (valid) {
                        ts.initialized = true;
                        sets.push_back(ts);
                    }
                }
            }
        }
        return sets;
    }

    // Match a character image against all templates, return best match
    static char templateMatch(const cv::Mat& charImg) {
        if (charImg.empty()) return '?';

        // Resize input to template size
        cv::Mat resized;
        cv::resize(charImg, resized, cv::Size(TMPL_W, TMPL_H), 0, 0, cv::INTER_AREA);
        cv::threshold(resized, resized, 64, 255, cv::THRESH_BINARY);

        auto& sets = getTemplateSets();
        if (sets.empty()) return '?';

        char bestChar = '?';
        double bestScore = -1.0;

        for (auto& ts : sets) {
            if (!ts.initialized) continue;
            for (int d = 0; d <= 9; ++d) {
                // Normalized cross-correlation (pixel overlap ratio)
                cv::Mat andResult, orResult;
                cv::bitwise_and(resized, ts.templates[d], andResult);
                cv::bitwise_or(resized, ts.templates[d], orResult);

                int intersect = cv::countNonZero(andResult);
                int unionPixels = cv::countNonZero(orResult);

                // IoU (Intersection over Union) as similarity score
                double iou = unionPixels > 0 ?
                    static_cast<double>(intersect) / unionPixels : 0;

                if (iou > bestScore) {
                    bestScore = iou;
                    bestChar = '0' + d;
                }
            }
        }

        // Only accept if confidence is reasonable
        if (bestScore > 0.35) {
            return bestChar;
        }
        return '?';
    }

    // Structural feature fallback for when template matching is uncertain
    static char structuralFallback(const cv::Mat& charImg) {
        if (charImg.empty()) return '?';
        int h = charImg.rows;
        int w = charImg.cols;
        float aspectRatio = static_cast<float>(w) / h;

        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(charImg.clone(), contours, hierarchy,
                         cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

        int holes = 0;
        for (size_t i = 0; i < contours.size(); ++i) {
            if (hierarchy[i][3] >= 0) ++holes;
        }

        float density = static_cast<float>(cv::countNonZero(charImg)) / (w * h);

        if (holes >= 2) return '8';
        if (holes == 1) {
            // Find hole center
            int holeCenterY = h / 2;
            for (size_t i = 0; i < contours.size(); ++i) {
                if (hierarchy[i][3] >= 0) {
                    cv::Moments m = cv::moments(contours[i]);
                    if (m.m00 > 0) holeCenterY = static_cast<int>(m.m01 / m.m00);
                    break;
                }
            }
            float holeRelY = static_cast<float>(holeCenterY) / h;
            if (holeRelY > 0.35 && holeRelY < 0.65) return '0';
            if (holeRelY > 0.5) return '6';
            return '9';
        }
        if (aspectRatio < 0.35) return '1';
        if (density > 0.55 && aspectRatio > 0.5) return '0';
        return '?';
    }

public:
    // Recognize a single digit/character from a binary image (white char on black bg)
    static char recognizeChar(const cv::Mat& charImg) {
        if (charImg.empty()) return '?';

        int h = charImg.rows;
        int w = charImg.cols;
        float aspectRatio = static_cast<float>(w) / h;
        float density = static_cast<float>(cv::countNonZero(charImg)) / (w * h);

        // Quick checks for non-digit characters
        if (h <= 4 && w <= 4) return '.';
        if (aspectRatio > 2.0 && density > 0.4 && h < w * 0.7) return '-';
        if (w < 6 && h < 6 && density > 0.3) return '.';

        // Primary: template matching
        char tmplResult = templateMatch(charImg);
        if (tmplResult != '?') return tmplResult;

        // Fallback: structural features
        return structuralFallback(charImg);
    }

    // Segment and recognize all characters in a binary image line.
    // Returns the recognized string.
    static std::string recognizeLine(const cv::Mat& binaryLine) {
        if (binaryLine.empty()) return "";

        // Find connected components
        cv::Mat labels, stats, centroids;
        int nLabels = cv::connectedComponentsWithStats(binaryLine, labels, stats, centroids);

        if (nLabels <= 1) return "";

        // Collect character bounding boxes, sorted left to right
        struct CharBox {
            int x, y, w, h, area;
            int label;
        };
        std::vector<CharBox> boxes;
        int imgH = binaryLine.rows;

        for (int i = 1; i < nLabels; ++i) {
            int x = stats.at<int>(i, cv::CC_STAT_LEFT);
            int y = stats.at<int>(i, cv::CC_STAT_TOP);
            int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
            int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
            int area = stats.at<int>(i, cv::CC_STAT_AREA);

            // Filter noise: too small or too large
            if (area < 4) continue;
            if (w > binaryLine.cols * 0.8 || h > binaryLine.rows * 0.9) continue;

            boxes.push_back({x, y, w, h, area, i});
        }

        if (boxes.empty()) return "";

        // Sort by x position
        std::sort(boxes.begin(), boxes.end(), [](const CharBox& a, const CharBox& b) {
            return a.x < b.x;
        });

        // Find median height to filter outliers
        std::vector<int> heights;
        for (auto& b : boxes) heights.push_back(b.h);
        std::sort(heights.begin(), heights.end());
        int medianH = heights[heights.size() / 2];

        // Recognize each character
        std::string result;
        int prevRight = -100;

        for (auto& box : boxes) {
            // Detect decimal point: significantly smaller than median height,
            // roughly square, positioned in the lower portion of the text line
            bool isDot = false;
            if (box.h < medianH * 0.5 && box.w < medianH * 0.5) {
                isDot = true;
            }
            if (box.h < medianH * 0.4 || (box.h < medianH * 0.6 && box.w < medianH * 0.6
                && static_cast<float>(box.w) / box.h > 0.5
                && static_cast<float>(box.w) / box.h < 2.0
                && box.y > imgH * 0.3)) {
                isDot = true;
            }

            if (isDot) {
                result += '.';
                prevRight = box.x + box.w;
                continue;
            }

            // Skip noise: too small to be any meaningful character
            if (box.h < medianH * 0.2 && box.w < medianH * 0.2) {
                continue;
            }

            // Extract character image
            cv::Rect charRect(box.x, box.y, box.w, box.h);
            charRect &= cv::Rect(0, 0, binaryLine.cols, binaryLine.rows);
            if (charRect.width <= 0 || charRect.height <= 0) continue;

            cv::Mat charImg = binaryLine(charRect).clone();

            // Add gap detection for space (not common in temp labels)
            char c = recognizeChar(charImg);
            if (c != '?') {
                result += c;
            }
            prevRight = box.x + box.w;
        }

        return result;
    }

    // Full pipeline: preprocess ROI image and recognize the number.
    // Input: BGR color image of the region containing the number.
    // Returns: all candidate numbers found.
    static std::vector<double> recognizeNumbers(const cv::Mat& roiColor) {
        std::vector<double> results;
        if (roiColor.empty()) return results;

        // Upscale small ROIs aggressively for better recognition
        cv::Mat scaled;
        int scale = 1;
        if (roiColor.rows < 15) scale = 6;
        else if (roiColor.rows < 25) scale = 4;
        else if (roiColor.rows < 40) scale = 3;
        else if (roiColor.rows < 60) scale = 2;

        if (scale > 1) {
            cv::resize(roiColor, scaled, cv::Size(), scale, scale, cv::INTER_CUBIC);
        } else {
            scaled = roiColor;
        }

        // Multiple binarization strategies optimized for white text on colored bg
        std::vector<cv::Mat> binaries;

        // Strategy 1: HSV white mask (best for white text on any colored bg)
        {
            cv::Mat hsv;
            cv::cvtColor(scaled, hsv, cv::COLOR_BGR2HSV);
            cv::Mat whiteMask;
            cv::inRange(hsv, cv::Scalar(0, 0, 160), cv::Scalar(180, 100, 255), whiteMask);
            binaries.push_back(whiteMask);
        }

        // Strategy 2: Grayscale brightness threshold
        {
            cv::Mat gray;
            cv::cvtColor(scaled, gray, cv::COLOR_BGR2GRAY);
            cv::Mat bright;
            cv::threshold(gray, bright, 200, 255, cv::THRESH_BINARY);
            binaries.push_back(bright);
        }

        // Strategy 3: Lab L-channel + Otsu
        {
            cv::Mat labImg;
            cv::cvtColor(scaled, labImg, cv::COLOR_BGR2Lab);
            std::vector<cv::Mat> ch;
            cv::split(labImg, ch);
            cv::equalizeHist(ch[0], ch[0]);
            cv::Mat otsu;
            cv::threshold(ch[0], otsu, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            // Ensure text is white (foreground)
            if (cv::countNonZero(otsu) > (int)otsu.total() / 2)
                cv::bitwise_not(otsu, otsu);
            binaries.push_back(otsu);
        }

        // Strategy 4: Grayscale Otsu + inverse
        {
            cv::Mat gray;
            cv::cvtColor(scaled, gray, cv::COLOR_BGR2GRAY);
            cv::Mat otsu;
            cv::threshold(gray, otsu, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            binaries.push_back(otsu);
            cv::Mat inv;
            cv::bitwise_not(otsu, inv);
            binaries.push_back(inv);
        }

        for (auto& bin : binaries) {
            // Morphological cleanup
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
            cv::Mat cleaned;
            cv::morphologyEx(bin, cleaned, cv::MORPH_CLOSE, kernel);

            std::string text = recognizeLine(cleaned);
            if (text.empty()) continue;

            std::regex numPattern(R"([-+]?\d+\.?\d*)");
            auto it = std::sregex_iterator(text.begin(), text.end(), numPattern);
            auto end = std::sregex_iterator();

            for (; it != end; ++it) {
                try {
                    double val = std::stod(it->str());
                    if (val > -200 && val < 10000 && std::abs(val) > 0.01) {
                        bool isDup = false;
                        for (double existing : results) {
                            if (std::abs(existing - val) < 0.001) { isDup = true; break; }
                        }
                        if (!isDup) results.push_back(val);
                    }
                } catch (...) {}
            }
        }

        return results;
    }
};

// ============================================================================
// Auto-detect the color bar region on the right side of the image.
//
// The key insight: a color bar has a MONOTONIC color gradient from top to
// bottom, with high HORIZONTAL UNIFORMITY within each row. The main thermal
// image has chaotic color patterns. We exploit this difference.
//
// Algorithm:
// 1. For each column in the right 30% of the image, compute:
//    a. Monotonicity score: how consistently does the L channel increase or
//       decrease from top to bottom? (bar: high, body: low)
//    b. Row uniformity: how similar are neighboring pixels in the same row?
//       (bar: very similar, body: varies)
// 2. Find narrow connected groups of columns that score high on both metrics.
// 3. Prefer the rightmost, narrowest qualifying group (color bars are typically
//    at the far right edge).
// 4. Determine vertical extent by finding where the monotonic gradient starts/ends.
// ============================================================================

ColorBarInfo detectColorBar(const cv::Mat& image) {
    int h = image.rows;
    int w = image.cols;

    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);

    // Search the rightmost 30% of the image
    int searchStart = static_cast<int>(w * 0.70);

    // --- Step 1: Score each column ---
    struct ColScore {
        int x;
        double dirConsistency; // consecutive color-delta direction consistency (0~1)
        double rowUniformity;  // how uniform is each row within a local neighborhood (0~1)
    };
    std::vector<ColScore> scores;

    // Vertical sampling range: avoid the very top/bottom (might have text/borders)
    int sampleTop = h / 8;
    int sampleBot = 7 * h / 8;
    int sampleStep = std::max(1, (sampleBot - sampleTop) / 100);

    for (int x = searchStart; x < w; ++x) {
        // --- Direction consistency: for each pair of consecutive steps, compute
        // the Lab color delta vector. A color bar has deltas that point in the
        // same direction (positive dot product). Random image content does not. ---
        int consistentPairs = 0, totalPairs = 0;
        for (int y = sampleTop; y < sampleBot - sampleStep * 2; y += sampleStep) {
            cv::Vec3f c1(lab.at<cv::Vec3b>(y, x));
            cv::Vec3f c2(lab.at<cv::Vec3b>(y + sampleStep, x));
            cv::Vec3f c3(lab.at<cv::Vec3b>(y + sampleStep * 2, x));
            // Delta vectors in Lab space
            float dL1 = c2[0] - c1[0], da1 = c2[1] - c1[1], db1 = c2[2] - c1[2];
            float dL2 = c3[0] - c2[0], da2 = c3[1] - c2[1], db2 = c3[2] - c2[2];
            // Dot product: positive means same direction
            float dot = dL1 * dL2 + da1 * da2 + db1 * db2;
            if (dot > 0) ++consistentPairs;
            ++totalPairs;
        }
        double dirCon = totalPairs > 0 ?
            static_cast<double>(consistentPairs) / totalPairs : 0;

        // --- Row uniformity: compare this column with neighbors (±2 pixels) ---
        double uniformSum = 0;
        int uniformCount = 0;
        for (int y = sampleTop; y < sampleBot; y += sampleStep * 2) {
            cv::Vec3f center(lab.at<cv::Vec3b>(y, x));
            double maxDiff = 0;
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = x + dx;
                if (nx < 0 || nx >= w || dx == 0) continue;
                cv::Vec3f neighbor(lab.at<cv::Vec3b>(y, nx));
                double diff = std::abs(center[0] - neighbor[0])
                            + std::abs(center[1] - neighbor[1])
                            + std::abs(center[2] - neighbor[2]);
                maxDiff = std::max(maxDiff, diff);
            }
            uniformSum += 1.0 / (1.0 + maxDiff / 10.0);
            ++uniformCount;
        }
        double rowUnif = uniformCount > 0 ? uniformSum / uniformCount : 0;

        scores.push_back({x, dirCon, rowUnif});
    }

    // --- Step 2: Find candidate bar regions ---
    // Color bar: high direction consistency (>0.6) AND moderate row uniformity (>0.3)
    double dirThresh = 0.60;
    double unifThresh = 0.30;

    struct BarCandidate {
        int startX, endX;
        double score;
    };
    std::vector<BarCandidate> candidates;

    size_t i = 0;
    while (i < scores.size()) {
        if (scores[i].dirConsistency >= dirThresh && scores[i].rowUniformity >= unifThresh) {
            int startX = scores[i].x;
            int endX = startX;
            double totalScore = scores[i].dirConsistency + scores[i].rowUniformity;
            int count = 1;

            // Extend the run of qualifying columns (allow slightly lower thresholds
            // for continuity, and allow up to 1 gap column for robustness)
            size_t j = i + 1;
            while (j < scores.size()
                   && scores[j].x <= scores[j-1].x + 2
                   && scores[j].dirConsistency >= dirThresh * 0.7
                   && scores[j].rowUniformity >= unifThresh * 0.5) {
                endX = scores[j].x;
                totalScore += scores[j].dirConsistency + scores[j].rowUniformity;
                ++count;
                ++j;
            }

            int barWidth = endX - startX + 1;
            // Color bar is typically 5-60 pixels wide
            if (barWidth >= 3 && barWidth <= std::max(60, w / 8)) {
                double avgScore = totalScore / count;
                candidates.push_back({startX, endX, avgScore});
            }
            i = j;
        } else {
            ++i;
        }
    }

    // --- Step 3: Select the best candidate ---
    // Prefer: rightmost position (bars are at the edge), then highest score
    int bestStart = -1, bestEnd = -1;
    double bestRank = -1;

    for (auto& c : candidates) {
        // Rank: heavily weight rightward position, then quality
        double rightness = static_cast<double>(c.startX) / w;
        double rank = rightness * 5.0 + c.score;
        if (rank > bestRank) {
            bestRank = rank;
            bestStart = c.startX;
            bestEnd = c.endX;
        }
    }

    // --- Step 4: Determine vertical extent ---
    // Strategy: use the color-direction consistency metric along the bar column.
    // Inside the color bar, consecutive rows have smooth, consistent color changes.
    // At the boundary (background, text, border), color changes become abrupt or
    // direction reverses. We find the longest segment with consistent direction.
    //
    // This is the same principle as the horizontal detection, applied vertically
    // on a single column, and is much more precise than brightness/saturation checks.
    int topY = 0, bottomY = h - 1;
    if (bestStart >= 0) {
        int midX = (bestStart + bestEnd) / 2;

        // Compute per-row color delta (in Lab) relative to the next row
        // Then check direction consistency between consecutive deltas
        struct RowDelta {
            float dL, da, db;
        };
        std::vector<RowDelta> deltas(h);
        for (int y = 0; y < h - 1; ++y) {
            cv::Vec3f c1(lab.at<cv::Vec3b>(y, midX));
            cv::Vec3f c2(lab.at<cv::Vec3b>(y + 1, midX));
            deltas[y] = {c2[0] - c1[0], c2[1] - c1[1], c2[2] - c1[2]};
        }
        deltas[h - 1] = {0, 0, 0};

        // For each row, compute a local "gradient activity" over a small window.
        // A color bar row has: (1) non-zero gradient locally AND (2) consistent
        // direction with neighbors. A background row has near-zero gradient.
        // We combine both checks: the row must be inside a gradient region.
        std::vector<bool> isBarRow(h, false);
        int windowR = std::max(2, h / 100); // local window half-size for gradient check

        for (int y = windowR; y < h - windowR; ++y) {
            // Check gradient activity: color must change noticeably within
            // a small window around this row (bar has continuous gradient)
            cv::Vec3f cTop(lab.at<cv::Vec3b>(y - windowR, midX));
            cv::Vec3f cBot(lab.at<cv::Vec3b>(y + windowR, midX));
            float localDeltaSq = (cBot[0] - cTop[0]) * (cBot[0] - cTop[0])
                               + (cBot[1] - cTop[1]) * (cBot[1] - cTop[1])
                               + (cBot[2] - cTop[2]) * (cBot[2] - cTop[2]);
            // Require a noticeable color change within the window
            // (filters out flat background even if it's colorful)
            if (localDeltaSq < 4.0f) continue;

            // Check direction consistency: delta at y and delta at y+1
            // should point in the same direction
            float dot = deltas[y].dL * deltas[y + 1].dL
                      + deltas[y].da * deltas[y + 1].da
                      + deltas[y].db * deltas[y + 1].db;
            isBarRow[y] = (dot >= 0);
        }

        // Find the longest continuous run of bar rows (allow small gaps)
        int bestRunStart = 0, bestRunLen = 0;
        int curRunStart = -1, curRunLen = 0;
        int gapCount = 0;
        const int maxGap = 3;

        for (int y = 0; y < h; ++y) {
            if (isBarRow[y]) {
                if (curRunStart < 0) {
                    curRunStart = y;
                    curRunLen = 1;
                } else {
                    curRunLen = y - curRunStart + 1;
                }
                gapCount = 0;
            } else {
                if (curRunStart >= 0 && gapCount < maxGap) {
                    ++gapCount;
                    curRunLen = y - curRunStart + 1;
                } else {
                    if (curRunLen > bestRunLen) {
                        bestRunLen = curRunLen;
                        bestRunStart = curRunStart;
                    }
                    curRunStart = -1;
                    curRunLen = 0;
                    gapCount = 0;
                }
            }
        }
        if (curRunLen > bestRunLen) {
            bestRunLen = curRunLen;
            bestRunStart = curRunStart;
        }

        if (bestRunLen > h / 8) {
            topY = bestRunStart;
            bottomY = bestRunStart + bestRunLen - 1;
            while (bottomY > topY && !isBarRow[bottomY]) --bottomY;
            while (topY < bottomY && !isBarRow[topY]) ++topY;

            // --- Step 4b: Refine boundaries using row-internal uniformity ---
            //
            // Inside the color bar, ALL pixels across the bar width at a given
            // row have nearly identical color (it's a uniform gradient strip).
            // At the text regions (e.g. "38.0"), pixels contain a mix of text
            // strokes and background, creating high color variance across the row.
            //
            // We check variance across [bestStart-margin, bestEnd+margin] to
            // also catch text that is adjacent to (not overlapping) the bar.

            // Row uniformity: measure the color variance WITHIN the bar width.
            // In the bar, all pixels at a given row are the same color → low var.
            // In text regions, text strokes overlay the bar → high var.
            // We use the standard deviation of Lab values across bar columns.
            auto rowColorStdDev = [&](int y) -> double {
                double sumL = 0, sumA = 0, sumB = 0;
                double sumL2 = 0, sumA2 = 0, sumB2 = 0;
                int n = 0;
                for (int x = bestStart; x <= bestEnd; ++x) {
                    cv::Vec3b px = lab.at<cv::Vec3b>(y, x);
                    sumL += px[0]; sumA += px[1]; sumB += px[2];
                    sumL2 += px[0]*px[0]; sumA2 += px[1]*px[1]; sumB2 += px[2]*px[2];
                    ++n;
                }
                if (n < 2) return 0;
                double varL = sumL2 / n - (sumL / n) * (sumL / n);
                double varA = sumA2 / n - (sumA / n) * (sumA / n);
                double varB = sumB2 / n - (sumB / n) * (sumB / n);
                return std::sqrt(std::max(0.0, varL) + std::max(0.0, varA) + std::max(0.0, varB));
            };

            // Compute baseline stddev from the safe center 50% of the bar
            int safeTop = topY + (bottomY - topY) / 4;
            int safeBot = topY + 3 * (bottomY - topY) / 4;
            double baselineStd = 0;
            for (int y = safeTop; y <= safeBot; y += std::max(1, (safeBot-safeTop)/30)) {
                baselineStd = std::max(baselineStd, rowColorStdDev(y));
            }
            // Text rows have much higher stddev (text strokes vs bar color)
            // Threshold: significantly above baseline; at least 3.0 to avoid
            // noise-level fluctuations in a pure color bar (stddev ≈ 0~2)
            double stdThreshold = std::max(baselineStd * 5.0, 3.0);

            // Trim from top
            while (topY < safeTop && rowColorStdDev(topY) > stdThreshold) ++topY;
            // Trim from bottom
            while (bottomY > safeBot && rowColorStdDev(bottomY) > stdThreshold) --bottomY;

            // Compensate for windowR: extend outward if rows are still uniform
            for (int dy = 0; dy < windowR + 2 && topY - 1 >= 0; ++dy) {
                if (rowColorStdDev(topY - 1) <= stdThreshold) --topY; else break;
            }
            for (int dy = 0; dy < windowR + 2 && bottomY + 1 < h; ++dy) {
                if (rowColorStdDev(bottomY + 1) <= stdThreshold) ++bottomY; else break;
            }
        }
    }

    if (bestStart < 0) {
        std::cerr << "[Warning] Auto-detection of color bar failed, using default region (rightmost 3%)\n";
        bestStart = static_cast<int>(w * 0.95);
        bestEnd = w - 1;
        topY = static_cast<int>(h * 0.05);
        bottomY = static_cast<int>(h * 0.95);
    }

    topY = std::max(0, topY);
    bottomY = std::min(h - 1, bottomY);
    bestStart = std::max(0, bestStart);
    bestEnd = std::min(w - 1, bestEnd);

    // Safety: ensure valid range (topY < bottomY with reasonable height)
    if (topY >= bottomY || (bottomY - topY) < h / 8) {
        std::cerr << "[Warning] Vertical extent detection failed (topY=" << topY
                  << ", bottomY=" << bottomY << "), using full range\n";
        topY = static_cast<int>(h * 0.05);
        bottomY = static_cast<int>(h * 0.95);
    }

    ColorBarInfo info;
    info.region = cv::Rect(bestStart, topY, bestEnd - bestStart + 1, bottomY - topY + 1);

    std::cout << "  Candidates evaluated: " << candidates.size() << "\n";

    // --- Step 5: Sample colors along the color bar ---
    int sampleCount = bottomY - topY + 1;
    info.labColors.resize(sampleCount);

    cv::Mat labForSample;
    cv::cvtColor(image, labForSample, cv::COLOR_BGR2Lab);

    for (int y = topY; y <= bottomY; ++y) {
        cv::Vec3f avgColor(0, 0, 0);
        int count = 0;
        // Average across the inner portion of the bar to avoid edge effects
        int margin = std::max(1, (bestEnd - bestStart + 1) / 4);
        for (int x = bestStart + margin; x <= bestEnd - margin; ++x) {
            cv::Vec3b c = labForSample.at<cv::Vec3b>(y, x);
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

// ============================================================================
// Temperature label recognition — multi-label scanning approach
//
// Instead of trying to read only the top/bottom labels (which are often the
// hardest — small text, decimal point, colored background), this approach:
//
// 1. Scans a tall strip next to the color bar for ALL white text blobs
// 2. Groups blobs into text lines by Y position
// 3. Recognizes each line independently
// 4. Uses multiple successful readings + their Y positions to perform a
//    LINEAR FIT (temperature vs Y position)
// 5. Extrapolates the fit to the bar top/bottom to get max/min temperature
//
// This is far more robust because:
// - Integer labels (37, 36, 35...) are easier to read than decimals (38.0)
// - Even if some labels fail, 2-3 successes are enough for a good fit
// - The linear fit naturally filters out misreadings as outliers
// ============================================================================

// Scan for all temperature labels near the color bar, return (y_position, value) pairs
std::vector<std::pair<double, double>> scanAllLabels(
    const cv::Mat& image, const ColorBarInfo& colorBar) {

    int imgW = image.cols;
    int imgH = image.rows;
    int barX = colorBar.region.x;
    int barY = colorBar.region.y;
    int barW = colorBar.region.width;
    int barH = colorBar.region.height;
    int barRight = barX + barW;
    int barBottom = barY + barH;

    // Define the search strip: a tall vertical band next to the bar
    int stripX1 = std::max(0, barX - 60);
    int stripX2 = std::min(imgW - 1, barRight + 60);
    int stripY1 = std::max(0, barY - 30);
    int stripY2 = std::min(imgH - 1, barBottom + 30);

    if (stripX2 <= stripX1 || stripY2 <= stripY1) return {};

    cv::Mat strip = image(cv::Rect(stripX1, stripY1, stripX2 - stripX1 + 1, stripY2 - stripY1 + 1));

    // Extract white/bright text using HSV thresholding
    cv::Mat hsv;
    cv::cvtColor(strip, hsv, cv::COLOR_BGR2HSV);
    cv::Mat whiteMask;
    cv::inRange(hsv, cv::Scalar(0, 0, 160), cv::Scalar(180, 100, 255), whiteMask);

    // Also try extracting bright pixels from grayscale (for non-white bright text)
    cv::Mat gray;
    cv::cvtColor(strip, gray, cv::COLOR_BGR2GRAY);
    cv::Mat brightMask;
    cv::threshold(gray, brightMask, 200, 255, cv::THRESH_BINARY);

    // Combine both masks
    cv::Mat textMask;
    cv::bitwise_or(whiteMask, brightMask, textMask);

    // Morphological cleanup: close small gaps in characters, remove noise
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(textMask, textMask, cv::MORPH_CLOSE, kernel);

    // Exclude the color bar column itself (it's not text)
    int localBarX1 = barX - stripX1;
    int localBarX2 = barRight - stripX1;
    for (int y = 0; y < textMask.rows; ++y) {
        for (int x = std::max(0, localBarX1 - 1); x <= std::min(textMask.cols - 1, localBarX2 + 1); ++x) {
            textMask.at<uchar>(y, x) = 0;
        }
    }

    // Find connected components in the text mask
    cv::Mat labels, stats, centroids;
    int nLabels = cv::connectedComponentsWithStats(textMask, labels, stats, centroids);

    // Group components into text lines by Y center position
    struct TextBlob {
        int x, y, w, h, area;
        double centerY;
    };
    std::vector<TextBlob> blobs;

    for (int i = 1; i < nLabels; ++i) {
        int x = stats.at<int>(i, cv::CC_STAT_LEFT);
        int y = stats.at<int>(i, cv::CC_STAT_TOP);
        int bw = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int bh = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 5 || bh < 3 || bw < 2) continue;
        if (bw > strip.cols * 0.8 || bh > strip.rows * 0.3) continue;
        blobs.push_back({x, y, bw, bh, area, y + bh / 2.0});
    }

    if (blobs.empty()) return {};

    // Sort by Y
    std::sort(blobs.begin(), blobs.end(),
              [](const TextBlob& a, const TextBlob& b) { return a.centerY < b.centerY; });

    // Group blobs into text lines (blobs with similar Y center)
    struct TextLine {
        int x1, y1, x2, y2;
        double centerY;
    };
    std::vector<TextLine> lines;
    {
        int medianH = 0;
        std::vector<int> heights;
        for (auto& b : blobs) heights.push_back(b.h);
        std::sort(heights.begin(), heights.end());
        medianH = heights[heights.size() / 2];
        int mergeThresh = std::max(5, medianH);

        size_t i = 0;
        while (i < blobs.size()) {
            TextLine line;
            line.x1 = blobs[i].x;
            line.y1 = blobs[i].y;
            line.x2 = blobs[i].x + blobs[i].w;
            line.y2 = blobs[i].y + blobs[i].h;
            line.centerY = blobs[i].centerY;
            int count = 1;

            size_t j = i + 1;
            while (j < blobs.size() && blobs[j].centerY - blobs[i].centerY < mergeThresh) {
                line.x1 = std::min(line.x1, blobs[j].x);
                line.y1 = std::min(line.y1, blobs[j].y);
                line.x2 = std::max(line.x2, blobs[j].x + blobs[j].w);
                line.y2 = std::max(line.y2, blobs[j].y + blobs[j].h);
                line.centerY += blobs[j].centerY;
                ++count;
                ++j;
            }
            line.centerY /= count;
            // Line must have reasonable width (at least 2 characters wide)
            if (line.x2 - line.x1 > medianH * 0.5) {
                lines.push_back(line);
            }
            i = j;
        }
    }

    // For each text line, extract ROI, recognize the number.
    // Use voting: each binarization strategy produces candidates; the value
    // that appears most frequently (or is most "reasonable") wins for that line.
    std::vector<std::pair<double, double>> results; // (y_in_image, temperature_value)

    for (auto& line : lines) {
        int pad = 3;
        int rx1 = std::max(0, line.x1 - pad);
        int ry1 = std::max(0, line.y1 - pad);
        int rx2 = std::min(strip.cols - 1, line.x2 + pad);
        int ry2 = std::min(strip.rows - 1, line.y2 + pad);
        if (rx2 <= rx1 || ry2 <= ry1) continue;

        cv::Mat lineROI = strip(cv::Rect(rx1, ry1, rx2 - rx1, ry2 - ry1));

        auto allNums = BuiltinDigitRecognizer::recognizeNumbers(lineROI);
        if (allNums.empty()) continue;

        // Vote: count how many times each value (rounded to 0.1) appears
        std::map<int, std::pair<double, int>> votes; // key=val*10, value=(exact_val, count)
        for (double val : allNums) {
            if (val < -100 || val > 10000) continue;
            int key = static_cast<int>(std::round(val * 10));
            auto it = votes.find(key);
            if (it != votes.end()) {
                it->second.second++;
            } else {
                votes[key] = {val, 1};
            }
        }

        // Pick the value with most votes; break ties by preferring
        // values in a "reasonable" temperature range (e.g., -50 to 500)
        double bestVal = allNums[0];
        int bestVotes = 0;
        for (auto& [key, vp] : votes) {
            bool isReasonable = (vp.first > -50 && vp.first < 500);
            int score = vp.second * 2 + (isReasonable ? 1 : 0);
            if (score > bestVotes || (score == bestVotes && isReasonable)) {
                bestVotes = score;
                bestVal = vp.first;
            }
        }

        double yInImage = stripY1 + line.centerY;
        results.push_back({yInImage, bestVal});
        std::cout << "    Label at y=" << (int)yInImage << ": " << bestVal << "\n";
    }

    return results;
}

// Use RANSAC-style robust fitting on (y, temperature) pairs.
// For each pair of readings, compute a linear model and count inliers.
// The model with most inliers wins.
bool fitTemperatureRange(const std::vector<std::pair<double, double>>& readings,
                         const ColorBarInfo& colorBar,
                         double& maxTemp, double& minTemp) {
    if (readings.size() < 2) return false;

    int barTop = colorBar.region.y;
    int barBottom = colorBar.region.y + colorBar.region.height;

    // If only 2 readings, just use them directly
    if (readings.size() == 2) {
        double y1 = readings[0].first, t1 = readings[0].second;
        double y2 = readings[1].first, t2 = readings[1].second;
        if (std::abs(y2 - y1) < 1) return false;
        double a = (t2 - t1) / (y2 - y1);
        maxTemp = t1 + a * (barTop - y1);
        minTemp = t1 + a * (barBottom - y1);
        if (maxTemp < minTemp) std::swap(maxTemp, minTemp);
        maxTemp = std::round(maxTemp * 10.0) / 10.0;
        minTemp = std::round(minTemp * 10.0) / 10.0;
        return true;
    }

    // RANSAC: try all pairs, find the model with most inliers
    double bestA = 0, bestB = 0;
    int bestInliers = 0;
    double inlierThresh = 1.5; // allow 1.5 degree error for inliers

    for (size_t i = 0; i < readings.size(); ++i) {
        for (size_t j = i + 1; j < readings.size(); ++j) {
            double y1 = readings[i].first, t1 = readings[i].second;
            double y2 = readings[j].first, t2 = readings[j].second;
            if (std::abs(y2 - y1) < 5) continue;

            double a = (t2 - t1) / (y2 - y1);
            double b = t1 - a * y1;

            // Temperature should decrease as y increases (typically)
            // Slope should be in a reasonable range
            double tempRange = std::abs(a * (barBottom - barTop));
            if (tempRange < 1 || tempRange > 500) continue;

            // Count inliers
            int inliers = 0;
            for (auto& [yPos, temp] : readings) {
                double predicted = a * yPos + b;
                if (std::abs(predicted - temp) <= inlierThresh) {
                    ++inliers;
                }
            }

            if (inliers > bestInliers) {
                bestInliers = inliers;
                bestA = a;
                bestB = b;
            }
        }
    }

    if (bestInliers < 2) {
        // Fallback: just use linear regression on all points
        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        int n = (int)readings.size();
        for (auto& [yPos, temp] : readings) {
            sumX += yPos; sumY += temp;
            sumXX += yPos * yPos; sumXY += yPos * temp;
        }
        double denom = n * sumXX - sumX * sumX;
        if (std::abs(denom) < 1e-10) return false;
        bestA = (n * sumXY - sumX * sumY) / denom;
        bestB = (sumY * sumXX - sumX * sumXY) / denom;
    } else {
        // Refit using only inliers for better accuracy
        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        int n = 0;
        for (auto& [yPos, temp] : readings) {
            double predicted = bestA * yPos + bestB;
            if (std::abs(predicted - temp) <= inlierThresh) {
                sumX += yPos; sumY += temp;
                sumXX += yPos * yPos; sumXY += yPos * temp;
                ++n;
            }
        }
        if (n >= 2) {
            double denom = n * sumXX - sumX * sumX;
            if (std::abs(denom) > 1e-10) {
                bestA = (n * sumXY - sumX * sumY) / denom;
                bestB = (sumY * sumXX - sumX * sumXY) / denom;
            }
        }
        std::cout << "  RANSAC: " << bestInliers << "/" << readings.size()
                  << " inliers\n";
    }

    maxTemp = bestA * barTop + bestB;
    minTemp = bestA * barBottom + bestB;
    if (maxTemp < minTemp) std::swap(maxTemp, minTemp);

    maxTemp = std::round(maxTemp * 10.0) / 10.0;
    minTemp = std::round(minTemp * 10.0) / 10.0;

    return true;
}

// Built-in temperature label recognition
bool builtinTemperatureLabels(const cv::Mat& image, const ColorBarInfo& colorBar,
                              double& maxTemp, double& minTemp) {
    std::cout << "  Scanning for temperature labels...\n";
    auto readings = scanAllLabels(image, colorBar);

    if (readings.empty()) {
        std::cerr << "  [Warning] No labels found\n";
        return false;
    }

    std::cout << "  Found " << readings.size() << " label(s)\n";

    bool success = fitTemperatureRange(readings, colorBar, maxTemp, minTemp);

    if (success) {
        std::cout << "  [Built-in] Max temp: " << maxTemp << "\n";
        std::cout << "  [Built-in] Min temp: " << minTemp << "\n";
    }

    return success;
}

// ============================================================================
// Tesseract OCR temperature label recognition (optional, compile with -DUSE_TESSERACT)
// ============================================================================

#ifdef USE_TESSERACT

std::vector<double> extractNumbers(const std::string& text) {
    std::vector<double> results;
    std::string cleaned = text;
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
            if (val > -200 && val < 10000) results.push_back(val);
        } catch (...) {}
    }
    return results;
}

bool tesseractTemperatureLabels(const cv::Mat& image, const ColorBarInfo& colorBar,
                                double& maxTemp, double& minTemp) {
    // Reuse the same multi-label scan approach, using Tesseract for recognition
    // instead of built-in template matching
    std::cout << "  Scanning labels with Tesseract...\n";
    auto readings = scanAllLabels(image, colorBar);
    if (readings.empty()) return false;

    std::cout << "  Found " << readings.size() << " label(s), applying Tesseract refinement...\n";

    // Also try Tesseract on each detected text line for potentially better results
    auto tess = std::make_unique<tesseract::TessBaseAPI>();
    tess->SetVariable("debug_file", "/dev/null");
    if (tess->Init(nullptr, "eng", tesseract::OEM_LSTM_ONLY) != 0) {
        if (tess->Init(nullptr, "eng") != 0) {
            // Fall back to built-in results
            return fitTemperatureRange(readings, colorBar, maxTemp, minTemp);
        }
    }
    tess->SetVariable("tessedit_char_whitelist", "0123456789.-+");
    tess->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
    tess->End();

    bool success = fitTemperatureRange(readings, colorBar, maxTemp, minTemp);
    if (success) {
        std::cout << "  [Tesseract] Max temp: " << maxTemp << "\n";
        std::cout << "  [Tesseract] Min temp: " << minTemp << "\n";
    }
    return success;
}

#endif // USE_TESSERACT

// ============================================================================
// Temperature extraction core
// ============================================================================

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

// ============================================================================
// Main
// ============================================================================

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName
              << " <image_path> [max_temp min_temp] [options]\n\n"
              << "Arguments:\n"
              << "  image_path     Path to the infrared pseudo-color image\n"
              << "  max_temp       Maximum temperature (top of color bar) [auto-detected if omitted]\n"
              << "  min_temp       Minimum temperature (bottom of color bar) [auto-detected if omitted]\n\n"
              << "Options:\n"
              << "  --bar-x1 N         Color bar left x coordinate (manual override)\n"
              << "  --bar-x2 N         Color bar right x coordinate\n"
              << "  --bar-y1 N         Color bar top y coordinate\n"
              << "  --bar-y2 N         Color bar bottom y coordinate\n"
              << "  --output-csv FILE  Output temperature matrix as CSV\n"
              << "  --output-img FILE  Output temperature heatmap as image\n"
              << "  --show             Display result in a window\n"
              << "  --exclude-bar      Exclude the color bar region from output\n"
#ifdef USE_TESSERACT
              << "  --use-tesseract    Use Tesseract OCR instead of built-in recognizer\n"
#endif
              << "  --no-ocr           Disable auto-detection (requires manual temp input)\n\n"
              << "OCR engine: "
#ifdef USE_TESSERACT
              << "Built-in + Tesseract (compile-time option)\n"
#else
              << "Built-in only (no external dependency)\n"
#endif
              << "\nExamples:\n"
              << "  # Fully automatic (built-in digit recognition):\n"
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
    bool useTesseract = false;

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
        else if (arg == "--use-tesseract") useTesseract = true;
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
            if (count > 0) avgColor /= count;
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

    // Auto-detect temperature range if not provided
    if (!tempProvided) {
        if (noOcr) {
            std::cerr << "[Error] Temperature range not provided and OCR is disabled.\n"
                      << "  Please provide max_temp and min_temp, or remove --no-ocr.\n";
            return 1;
        }

        bool ocrSuccess = false;

#ifdef USE_TESSERACT
        if (useTesseract) {
            std::cout << "Auto-detecting temperature range via Tesseract OCR...\n";
            ocrSuccess = tesseractTemperatureLabels(image, colorBar, maxTemp, minTemp);
        } else {
            std::cout << "Auto-detecting temperature range via built-in recognizer...\n";
            ocrSuccess = builtinTemperatureLabels(image, colorBar, maxTemp, minTemp);
            if (!ocrSuccess) {
                std::cout << "  Built-in failed, trying Tesseract...\n";
                ocrSuccess = tesseractTemperatureLabels(image, colorBar, maxTemp, minTemp);
            }
        }
#else
        (void)useTesseract;
        std::cout << "Auto-detecting temperature range via built-in recognizer...\n";
        ocrSuccess = builtinTemperatureLabels(image, colorBar, maxTemp, minTemp);
#endif

        if (!ocrSuccess) {
            std::cerr << "[Error] Failed to detect temperature labels.\n"
                      << "  Please provide max_temp and min_temp as command-line arguments.\n"
                      << "  Example: " << argv[0] << " " << imagePath << " 45.2 22.1\n";
            return 1;
        }
        std::cout << "Detected temperature range: " << minTemp << " ~ " << maxTemp << "\n";
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

    std::cout << "Building temperature lookup table...\n";
    TemperatureLUT lut(colorBar.labColors, maxTemp, minTemp);

    int outX1 = 0, outY1 = 0;
    int outX2 = image.cols - 1, outY2 = image.rows - 1;
    if (excludeBar) {
        outX2 = std::min(outX2, colorBar.region.x - 1);
    }
    int outW = outX2 - outX1 + 1;
    int outH = outY2 - outY1 + 1;

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
