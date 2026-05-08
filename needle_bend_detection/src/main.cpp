#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

struct BendResult {
    cv::Point2d bend_point;
    double bend_angle;
    double bend_length;
    double straight_length;
    cv::Point2d tip_point;
    cv::Point2d tail_point;
    std::vector<cv::Point2d> centerline;
    cv::Vec4f straight_line_params;
    cv::Vec4f bend_line_params;   // 弯曲段中心线拟合直线参数
    int bend_idx;                 // 弯曲点在中心线中的索引
};

// 基于轮廓的上下边缘提取中心线（针对近水平的针体更精确）
std::vector<cv::Point2d> extractCenterlineFromMask(const cv::Mat& mask) {
    std::vector<cv::Point2d> centerline;

    // 找到mask中有内容的x范围
    int x_min = mask.cols, x_max = 0;
    for (int y = 0; y < mask.rows; y++) {
        for (int x = 0; x < mask.cols; x++) {
            if (mask.at<uchar>(y, x) > 0) {
                x_min = std::min(x_min, x);
                x_max = std::max(x_max, x);
            }
        }
    }

    // 对每个x列，找上下边缘的中点
    for (int x = x_min; x <= x_max; x++) {
        int y_top = -1, y_bot = -1;
        for (int y = 0; y < mask.rows; y++) {
            if (mask.at<uchar>(y, x) > 0) {
                if (y_top < 0) y_top = y;
                y_bot = y;
            }
        }
        if (y_top >= 0 && y_bot >= 0) {
            double cy = (y_top + y_bot) / 2.0;
            centerline.push_back(cv::Point2d(x, cy));
        }
    }

    return centerline;
}

// 亚像素精度中心线提取（使用灰度加权）
std::vector<cv::Point2d> extractSubpixelCenterline(const cv::Mat& gray, const cv::Mat& mask) {
    std::vector<cv::Point2d> centerline;

    int x_min = mask.cols, x_max = 0;
    for (int y = 0; y < mask.rows; y++) {
        for (int x = 0; x < mask.cols; x++) {
            if (mask.at<uchar>(y, x) > 0) {
                x_min = std::min(x_min, x);
                x_max = std::max(x_max, x);
            }
        }
    }

    // 对每个x列，用灰度加权计算亚像素中心
    for (int x = x_min; x <= x_max; x++) {
        double weight_sum = 0, wy_sum = 0;
        int y_top = -1, y_bot = -1;

        for (int y = 0; y < mask.rows; y++) {
            if (mask.at<uchar>(y, x) > 0) {
                if (y_top < 0) y_top = y;
                y_bot = y;
            }
        }

        if (y_top < 0) continue;

        // 在针体区域内用灰度反转值做权重
        for (int y = y_top; y <= y_bot; y++) {
            double w = 255.0 - gray.at<uchar>(y, x); // 越暗权重越大
            weight_sum += w;
            wy_sum += w * y;
        }

        if (weight_sum > 0) {
            double cy = wy_sum / weight_sum;
            centerline.push_back(cv::Point2d(x, cy));
        }
    }

    return centerline;
}

// 高斯平滑中心线
std::vector<cv::Point2d> smoothCenterline(const std::vector<cv::Point2d>& pts, int kernel_size) {
    int n = pts.size();
    if (n < kernel_size) return pts;

    std::vector<cv::Point2d> smoothed(n);
    int half = kernel_size / 2;

    std::vector<double> kernel(kernel_size);
    double sigma = kernel_size / 5.0;
    double sum = 0;
    for (int i = 0; i < kernel_size; i++) {
        double x = i - half;
        kernel[i] = std::exp(-x * x / (2 * sigma * sigma));
        sum += kernel[i];
    }
    for (auto& k : kernel) k /= sum;

    for (int i = 0; i < n; i++) {
        double sx = 0, sy = 0;
        for (int j = -half; j <= half; j++) {
            int idx = std::clamp(i + j, 0, n - 1);
            sx += pts[idx].x * kernel[j + half];
            sy += pts[idx].y * kernel[j + half];
        }
        smoothed[i] = cv::Point2d(sx, sy);
    }

    return smoothed;
}

// 计算累积偏差来确定弯曲起始点
// 从直线端开始，逐步检查每个点到参考直线的距离
int findBendStartPoint(const std::vector<cv::Point2d>& centerline,
                        const cv::Vec4f& ref_line,
                        double& adaptive_threshold) {
    int n = centerline.size();
    double vx = ref_line[0], vy = ref_line[1];
    double x0 = ref_line[2], y0 = ref_line[3];

    // 计算每个点到参考直线的有符号距离
    std::vector<double> distances(n);
    for (int i = 0; i < n; i++) {
        double dx = centerline[i].x - x0;
        double dy = centerline[i].y - y0;
        distances[i] = dx * vy - dy * vx; // 有符号距离
    }

    // 估计直线段的噪声水平（用前20%的点）
    int baseline_n = std::max(10, n / 5);
    double mean = 0;
    for (int i = 0; i < baseline_n; i++) mean += distances[i];
    mean /= baseline_n;

    double stddev = 0;
    for (int i = 0; i < baseline_n; i++) {
        stddev += (distances[i] - mean) * (distances[i] - mean);
    }
    stddev = std::sqrt(stddev / baseline_n);

    // 自适应阈值：超过 mean + 4*sigma 认为开始弯曲
    adaptive_threshold = std::abs(mean) + 4.0 * stddev + 1.0;

    // 使用滑动窗口寻找第一个连续超阈值区域
    int window = std::max(5, n / 50);
    for (int i = baseline_n; i < n - window; i++) {
        int exceed = 0;
        for (int j = i; j < i + window; j++) {
            if (std::abs(distances[j] - mean) > adaptive_threshold) exceed++;
        }
        if (exceed >= window * 0.6) {
            // 精确定位：在i附近找第一个超阈值点
            for (int j = std::max(0, i - window); j < i + window && j < n; j++) {
                if (std::abs(distances[j] - mean) > adaptive_threshold * 0.5) {
                    return j;
                }
            }
            return i;
        }
    }

    // Fallback: 使用最大梯度变化点
    double max_grad = 0;
    int max_grad_idx = n / 2;
    int grad_window = std::max(3, n / 30);
    for (int i = grad_window; i < n - grad_window; i++) {
        double grad = std::abs(distances[i + grad_window] - distances[i - grad_window]);
        if (grad > max_grad) {
            max_grad = grad;
            max_grad_idx = i;
        }
    }

    return max_grad_idx;
}

// 使用分段线性拟合精确找到弯曲点（高精度方法）
int findBendPointPiecewiseFit(const std::vector<cv::Point2d>& centerline) {
    int n = centerline.size();
    if (n < 20) return n / 2;

    double min_total_error = 1e18;
    int best_split = n / 2;

    // 对每个可能的分割点，计算两段的拟合残差之和
    int step = std::max(1, n / 200); // 精细搜索步长
    for (int split = n / 5; split < n * 4 / 5; split += step) {
        // 第一段拟合直线
        std::vector<cv::Point2f> seg1, seg2;
        for (int i = 0; i < split; i++)
            seg1.push_back(cv::Point2f(centerline[i].x, centerline[i].y));
        for (int i = split; i < n; i++)
            seg2.push_back(cv::Point2f(centerline[i].x, centerline[i].y));

        if (seg1.size() < 5 || seg2.size() < 5) continue;

        cv::Vec4f line1, line2;
        cv::fitLine(seg1, line1, cv::DIST_L2, 0, 0.01, 0.01);
        cv::fitLine(seg2, line2, cv::DIST_L2, 0, 0.01, 0.01);

        // 计算残差
        double err1 = 0, err2 = 0;
        for (const auto& p : seg1) {
            double dx = p.x - line1[2], dy = p.y - line1[3];
            double d = std::abs(dx * line1[1] - dy * line1[0]);
            err1 += d * d;
        }
        for (const auto& p : seg2) {
            double dx = p.x - line2[2], dy = p.y - line2[3];
            double d = std::abs(dx * line2[1] - dy * line2[0]);
            err2 += d * d;
        }

        double total_err = err1 / seg1.size() + err2 / seg2.size();

        // 加入正则项：鼓励第一段越直越好（残差越小越好）
        double straightness_bonus = -err1 / (seg1.size() * seg1.size());
        total_err += straightness_bonus * 0.1;

        if (total_err < min_total_error) {
            min_total_error = total_err;
            best_split = split;
        }
    }

    // 在best_split附近精细搜索
    int fine_start = std::max(5, best_split - n / 20);
    int fine_end = std::min(n - 5, best_split + n / 20);
    for (int split = fine_start; split < fine_end; split++) {
        std::vector<cv::Point2f> seg1, seg2;
        for (int i = 0; i < split; i++)
            seg1.push_back(cv::Point2f(centerline[i].x, centerline[i].y));
        for (int i = split; i < n; i++)
            seg2.push_back(cv::Point2f(centerline[i].x, centerline[i].y));

        if (seg1.size() < 5 || seg2.size() < 5) continue;

        cv::Vec4f line1, line2;
        cv::fitLine(seg1, line1, cv::DIST_L2, 0, 0.01, 0.01);
        cv::fitLine(seg2, line2, cv::DIST_L2, 0, 0.01, 0.01);

        double err1 = 0, err2 = 0;
        for (const auto& p : seg1) {
            double dx = p.x - line1[2], dy = p.y - line1[3];
            double d = std::abs(dx * line1[1] - dy * line1[0]);
            err1 += d * d;
        }
        for (const auto& p : seg2) {
            double dx = p.x - line2[2], dy = p.y - line2[3];
            double d = std::abs(dx * line2[1] - dy * line2[0]);
            err2 += d * d;
        }

        double total_err = err1 / seg1.size() + err2 / seg2.size();
        if (total_err < min_total_error) {
            min_total_error = total_err;
            best_split = split;
        }
    }

    return best_split;
}

BendResult detectNeedleBend(const cv::Mat& src) {
    BendResult result;

    // 1. 预处理
    cv::Mat gray, blurred, binary;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.5);

    // Otsu阈值 + 反转（针是深色）
    cv::threshold(blurred, binary, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    // 形态学去噪
    cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel_open, cv::Point(-1,-1), 2);
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel_close);

    // 2. 找最大连通区域
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    if (contours.empty()) {
        std::cerr << "Error: No contours found!" << std::endl;
        return result;
    }

    int max_idx = 0;
    double max_area = 0;
    for (int i = 0; i < (int)contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area > max_area) {
            max_area = area;
            max_idx = i;
        }
    }

    // 针体掩码
    cv::Mat needle_mask = cv::Mat::zeros(binary.size(), CV_8UC1);
    cv::drawContours(needle_mask, contours, max_idx, 255, cv::FILLED);

    std::cout << "Needle area: " << max_area << " pixels" << std::endl;

    // 3. 提取亚像素精度中心线
    std::vector<cv::Point2d> raw_centerline = extractSubpixelCenterline(gray, needle_mask);

    if (raw_centerline.size() < 20) {
        std::cerr << "Error: Centerline too short (" << raw_centerline.size() << " points)" << std::endl;
        // Fallback to simple extraction
        raw_centerline = extractCenterlineFromMask(needle_mask);
    }

    std::cout << "Centerline points: " << raw_centerline.size() << std::endl;

    if (raw_centerline.size() < 20) {
        std::cerr << "Error: Still too few centerline points!" << std::endl;
        return result;
    }

    // 确定针的方向：中心线第一个点x坐标 vs 最后一个点
    // 确保从右（直线端）到左（尖端）排序
    if (raw_centerline.front().x < raw_centerline.back().x) {
        std::reverse(raw_centerline.begin(), raw_centerline.end());
    }

    // 4. 平滑中心线
    int smooth_k = std::max(7, (int)raw_centerline.size() / 40);
    if (smooth_k % 2 == 0) smooth_k++;
    std::vector<cv::Point2d> centerline = smoothCenterline(raw_centerline, smooth_k);
    result.centerline = centerline;

    int n = centerline.size();
    result.tail_point = centerline.front(); // 右端（直线端）
    result.tip_point = centerline.back();   // 左端（针尖）

    // 5. 用直线端拟合参考直线
    int straight_fit_count = std::max(20, n / 4);
    std::vector<cv::Point2f> straight_pts;
    for (int i = 0; i < straight_fit_count; i++) {
        straight_pts.push_back(cv::Point2f(centerline[i].x, centerline[i].y));
    }
    cv::Vec4f ref_line;
    cv::fitLine(straight_pts, ref_line, cv::DIST_L2, 0, 0.01, 0.01);

    // 6. 高精度弯曲点检测 - 方法1：累积偏差
    double threshold;
    int bend_idx_1 = findBendStartPoint(centerline, ref_line, threshold);

    // 方法2：分段线性拟合
    int bend_idx_2 = findBendPointPiecewiseFit(centerline);

    // 融合两种方法的结果（取加权平均，偏向更保守的）
    int bend_idx = (bend_idx_1 + bend_idx_2) / 2;

    // 进一步精化：在区间内做更精细的梯度分析
    int refine_start = std::max(5, std::min(bend_idx_1, bend_idx_2) - 10);
    int refine_end = std::min(n - 5, std::max(bend_idx_1, bend_idx_2) + 10);

    double vx = ref_line[0], vy = ref_line[1];
    double x0 = ref_line[2], y0 = ref_line[3];

    // 计算距离的二阶导数（曲率变化最大处）
    std::vector<double> dist(n, 0);
    for (int i = 0; i < n; i++) {
        double dx = centerline[i].x - x0;
        double dy = centerline[i].y - y0;
        dist[i] = std::abs(dx * vy - dy * vx);
    }

    double max_second_deriv = 0;
    int refined_bend_idx = bend_idx;
    for (int i = refine_start + 2; i < refine_end - 2; i++) {
        double d2 = dist[i+2] - 2*dist[i] + dist[i-2];
        if (d2 > max_second_deriv) {
            max_second_deriv = d2;
            refined_bend_idx = i;
        }
    }

    bend_idx = refined_bend_idx;
    result.bend_point = centerline[bend_idx];

    std::cout << "Bend detection: method1=" << bend_idx_1 
              << " method2=" << bend_idx_2 
              << " refined=" << bend_idx << std::endl;

    // 7. 计算弯曲角度
    // 直线段方向：用弯曲点之前的中心线拟合
    std::vector<cv::Point2f> final_straight_pts;
    for (int i = 0; i < bend_idx; i++) {
        final_straight_pts.push_back(cv::Point2f(centerline[i].x, centerline[i].y));
    }
    if (final_straight_pts.size() >= 5) {
        cv::fitLine(final_straight_pts, ref_line, cv::DIST_L2, 0, 0.01, 0.01);
    }
    result.straight_line_params = ref_line;

    double vx_s = ref_line[0], vy_s = ref_line[1];

    // 弯曲段方向：用整个弯曲段的中心线拟合（而非仅针尖处）
    // 这样得到的是弯曲段的整体方向，避免针尖几何形状导致角度偏大
    std::vector<cv::Point2f> bend_section_pts;
    for (int i = bend_idx; i < n; i++) {
        bend_section_pts.push_back(cv::Point2f(centerline[i].x, centerline[i].y));
    }

    cv::Vec4f bend_line;
    if (bend_section_pts.size() >= 5) {
        cv::fitLine(bend_section_pts, bend_line, cv::DIST_L2, 0, 0.01, 0.01);
    } else {
        // 点太少时用首尾方向代替
        cv::Point2d dir = centerline.back() - centerline[bend_idx];
        double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        bend_line = cv::Vec4f(dir.x / len, dir.y / len, 
                              centerline[bend_idx].x, centerline[bend_idx].y);
    }
    double vx_t = bend_line[0], vy_t = bend_line[1];

    // 保证方向一致（从尾向尖的方向，即x递减方向）
    if (vx_s > 0) { vx_s = -vx_s; vy_s = -vy_s; }
    if (vx_t > 0) { vx_t = -vx_t; vy_t = -vy_t; }

    double dot = vx_s * vx_t + vy_s * vy_t;
    double cross = vx_s * vy_t - vy_s * vx_t;
    result.bend_angle = std::atan2(std::abs(cross), dot) * 180.0 / CV_PI;
    result.bend_line_params = bend_line;
    result.bend_idx = bend_idx;

    // 8. 弯曲弧长
    double arc_length = 0;
    for (int i = bend_idx; i < n - 1; i++) {
        double dx = centerline[i+1].x - centerline[i].x;
        double dy = centerline[i+1].y - centerline[i].y;
        arc_length += std::sqrt(dx * dx + dy * dy);
    }
    result.bend_length = arc_length;

    // 直线段长度
    double straight_len = 0;
    for (int i = 0; i < bend_idx - 1; i++) {
        double dx = centerline[i+1].x - centerline[i].x;
        double dy = centerline[i+1].y - centerline[i].y;
        straight_len += std::sqrt(dx * dx + dy * dy);
    }
    result.straight_length = straight_len;

    return result;
}

void drawResults(cv::Mat& vis, const BendResult& result) {
    int n = result.centerline.size();

    // 画中心线（绿色）
    for (int i = 0; i < n - 1; i++) {
        cv::Point p1(cvRound(result.centerline[i].x), cvRound(result.centerline[i].y));
        cv::Point p2(cvRound(result.centerline[i+1].x), cvRound(result.centerline[i+1].y));
        cv::line(vis, p1, p2, cv::Scalar(0, 200, 0), 1, cv::LINE_AA);
    }

    // 画参考直线（延伸到图像边缘，黄色）
    cv::Vec4f sl = result.straight_line_params;
    double vx = sl[0], vy = sl[1], x0 = sl[2], y0 = sl[3];
    if (std::abs(vx) > 1e-6) {
        cv::Point pt1(0, cvRound(y0 + (0 - x0) * vy / vx));
        cv::Point pt2(vis.cols, cvRound(y0 + (vis.cols - x0) * vy / vx));
        cv::line(vis, pt1, pt2, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }

    // 在弯曲区域画弯曲段中心线拟合直线（橙色）
    // 这条直线代表弯曲段的整体方向，而非弯曲点到针尖的连线
    cv::Vec4f bl = result.bend_line_params;
    double bvx = bl[0], bvy = bl[1], bx0 = bl[2], by0 = bl[3];
    cv::Point bend_pt(cvRound(result.bend_point.x), cvRound(result.bend_point.y));
    cv::Point tip_pt(cvRound(result.tip_point.x), cvRound(result.tip_point.y));

    if (std::abs(bvx) > 1e-6) {
        // 计算弯曲段直线在弯曲区域x范围内的两个端点
        double x_start = result.bend_point.x;
        double x_end = result.tip_point.x;
        double y_start = by0 + (x_start - bx0) * bvy / bvx;
        double y_end = by0 + (x_end - bx0) * bvy / bvx;
        cv::Point bl_pt1(cvRound(x_start), cvRound(y_start));
        cv::Point bl_pt2(cvRound(x_end), cvRound(y_end));
        cv::line(vis, bl_pt1, bl_pt2, cv::Scalar(0, 140, 255), 2, cv::LINE_AA);
    }

    // 标记弯曲点（红色圆圈）
    cv::circle(vis, bend_pt, 8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    cv::putText(vis, "Bend Start",
                cv::Point(bend_pt.x - 10, bend_pt.y - 15),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);

    // 标记针尖（紫色）
    cv::circle(vis, tip_pt, 6, cv::Scalar(255, 0, 200), 2, cv::LINE_AA);
    cv::putText(vis, "Tip",
                cv::Point(tip_pt.x - 5, tip_pt.y - 15),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 200), 1, cv::LINE_AA);

    // 标记针尾
    cv::Point tail_pt(cvRound(result.tail_point.x), cvRound(result.tail_point.y));
    cv::circle(vis, tail_pt, 5, cv::Scalar(255, 200, 0), 2, cv::LINE_AA);

    // 画弯曲角度弧线标注（用弯曲段拟合方向而非针尖方向）
    int arc_radius = 30;
    double angle_straight = std::atan2(vy, vx) * 180.0 / CV_PI;
    double angle_bend = std::atan2(bvy, bvx) * 180.0 / CV_PI;
    // 保证角度方向和弯曲段方向一致
    if (bvx > 0) angle_bend = std::atan2(-bvy, -bvx) * 180.0 / CV_PI;
    if (vx > 0) angle_straight = std::atan2(-vy, -vx) * 180.0 / CV_PI;
    cv::ellipse(vis, bend_pt, cv::Size(arc_radius, arc_radius),
                0, std::min(angle_straight, angle_bend), 
                std::max(angle_straight, angle_bend),
                cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

    // 角度数值标注
    std::string angle_text = cv::format("%.2f deg", result.bend_angle);
    cv::putText(vis, angle_text,
                cv::Point(bend_pt.x + 15, bend_pt.y + 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);

    // 弯曲长度标注
    int mid_bend = n * 3 / 4;
    if (mid_bend < n) {
        std::string len_text = cv::format("Arc=%.1fpx", result.bend_length);
        cv::putText(vis, len_text,
                    cv::Point(cvRound(result.centerline[mid_bend].x),
                              cvRound(result.centerline[mid_bend].y) + 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 180, 0), 1, cv::LINE_AA);
    }
}

int main(int argc, char** argv) {
    std::string image_path = "images/needle.jpg";
    if (argc > 1) {
        image_path = argv[1];
    }

    cv::Mat src = cv::imread(image_path);
    if (src.empty()) {
        std::cerr << "Error: Cannot load image: " << image_path << std::endl;
        std::cerr << "Usage: " << argv[0] << " <image_path>" << std::endl;
        return -1;
    }

    std::cout << "Input image: " << image_path << std::endl;
    std::cout << "Image size: " << src.cols << " x " << src.rows << std::endl;

    // 缩放大图以加速处理
    double scale = 1.0;
    int max_dim = std::max(src.cols, src.rows);
    if (max_dim > 2000) {
        scale = 2000.0 / max_dim;
        cv::resize(src, src, cv::Size(), scale, scale, cv::INTER_AREA);
        std::cout << "Resized to: " << src.cols << " x " << src.rows 
                  << " (scale=" << scale << ")" << std::endl;
    }

    // 执行检测
    BendResult result = detectNeedleBend(src);

    if (result.centerline.empty()) {
        std::cerr << "Error: Detection failed!" << std::endl;
        return -1;
    }

    // 输出结果（转换回原始尺寸坐标）
    std::cout << "\n============================================" << std::endl;
    std::cout << "    Needle Bend Detection Results" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::fixed;
    std::cout.precision(2);
    std::cout << "  Bend Angle:      " << result.bend_angle << " degrees" << std::endl;
    std::cout << "  Bend Arc Length:  " << result.bend_length / scale << " pixels" << std::endl;
    std::cout << "  Straight Length:  " << result.straight_length / scale << " pixels" << std::endl;
    std::cout << "  Bend Position:   (" 
              << result.bend_point.x / scale << ", " 
              << result.bend_point.y / scale << ")" << std::endl;
    std::cout << "  Tip Position:    (" 
              << result.tip_point.x / scale << ", "
              << result.tip_point.y / scale << ")" << std::endl;
    std::cout << "  Tail Position:   ("
              << result.tail_point.x / scale << ", "
              << result.tail_point.y / scale << ")" << std::endl;
    std::cout << "  Total Length:    " 
              << (result.straight_length + result.bend_length) / scale << " pixels" << std::endl;
    std::cout << "  Bend Ratio:      " 
              << result.bend_length / (result.straight_length + result.bend_length) * 100.0
              << "%" << std::endl;
    std::cout << "============================================\n" << std::endl;

    // 可视化
    cv::Mat vis = src.clone();
    drawResults(vis, result);

    // 顶部信息面板
    int panel_h = 90;
    cv::Mat output(vis.rows + panel_h, vis.cols, vis.type(), cv::Scalar(30, 30, 30));
    vis.copyTo(output(cv::Rect(0, panel_h, vis.cols, vis.rows)));

    int y_text = 22;
    cv::putText(output, cv::format("Bend Angle: %.2f deg", result.bend_angle),
                cv::Point(10, y_text), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(0, 255, 255), 1);
    cv::putText(output, cv::format("Bend Length: %.1f px (arc)", result.bend_length / scale),
                cv::Point(10, y_text + 22), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(0, 255, 200), 1);
    cv::putText(output, cv::format("Bend Position: (%.0f, %.0f)",
                result.bend_point.x / scale, result.bend_point.y / scale),
                cv::Point(10, y_text + 44), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(200, 200, 255), 1);
    cv::putText(output, cv::format("Straight Length: %.1f px", result.straight_length / scale),
                cv::Point(400, y_text), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(200, 255, 200), 1);
    cv::putText(output, cv::format("Total Length: %.1f px",
                (result.straight_length + result.bend_length) / scale),
                cv::Point(400, y_text + 22), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(220, 220, 220), 1);
    cv::putText(output, cv::format("Bend Ratio: %.1f%%",
                result.bend_length / (result.straight_length + result.bend_length) * 100.0),
                cv::Point(400, y_text + 44), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(220, 180, 255), 1);

    // 保存
    std::string output_path = "output_result.jpg";
    cv::imwrite(output_path, output);
    std::cout << "Result image saved to: " << output_path << std::endl;

    // 也保存中间结果方便调试
    cv::Mat debug;
    cv::cvtColor(cv::Mat::zeros(src.size(), CV_8UC1), debug, cv::COLOR_GRAY2BGR);
    cv::Mat gray_img;
    cv::cvtColor(src, gray_img, cv::COLOR_BGR2GRAY);
    cv::Mat binary_debug;
    cv::threshold(gray_img, binary_debug, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::cvtColor(binary_debug, debug, cv::COLOR_GRAY2BGR);
    drawResults(debug, result);
    cv::imwrite("output_debug.jpg", debug);
    std::cout << "Debug image saved to: output_debug.jpg" << std::endl;

    return 0;
}
