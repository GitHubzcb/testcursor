#include "thermal_human_detector.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thermal {

namespace {

cv::Mat normalize_to_8u(const cv::Mat& src) {
    cv::Mat gray8;
    if (src.type() == CV_8UC1) {
        gray8 = src.clone();
    } else if (src.type() == CV_16UC1) {
        double mn, mx;
        cv::minMaxLoc(src, &mn, &mx);
        if (mx - mn < 1e-6) {
            gray8 = cv::Mat::zeros(src.size(), CV_8UC1);
        } else {
            src.convertTo(gray8, CV_8UC1, 255.0 / (mx - mn), -mn * 255.0 / (mx - mn));
        }
    } else if (src.type() == CV_32FC1 || src.type() == CV_64FC1) {
        double mn, mx;
        cv::minMaxLoc(src, &mn, &mx);
        if (mx - mn < 1e-6) {
            gray8 = cv::Mat::zeros(src.size(), CV_8UC1);
        } else {
            src.convertTo(gray8, CV_8UC1, 255.0 / (mx - mn), -mn * 255.0 / (mx - mn));
        }
    } else {
        throw std::invalid_argument("Unsupported image type. Expected CV_8UC1, CV_16UC1, CV_32FC1, or CV_64FC1.");
    }
    return gray8;
}

cv::Mat segment_by_temperature(const cv::Mat& temp_matrix, double low, double high) {
    cv::Mat mask = cv::Mat::zeros(temp_matrix.size(), CV_8UC1);
    cv::Mat temp32;

    if (temp_matrix.type() == CV_64FC1)
        temp_matrix.convertTo(temp32, CV_32FC1);
    else if (temp_matrix.type() == CV_32FC1)
        temp32 = temp_matrix;
    else
        throw std::invalid_argument("Temperature matrix must be CV_32FC1 or CV_64FC1.");

    for (int r = 0; r < temp32.rows; ++r) {
        const float* row = temp32.ptr<float>(r);
        uchar* mask_row = mask.ptr<uchar>(r);
        for (int c = 0; c < temp32.cols; ++c) {
            if (row[c] >= low && row[c] <= high)
                mask_row[c] = 255;
        }
    }
    return mask;
}

cv::Mat segment_adaptive(const cv::Mat& gray8, const DetectorParams& params) {
    cv::Mat blurred, binary;

    cv::GaussianBlur(gray8, blurred, cv::Size(5, 5), 1.5);

    int block_size = params.adaptive_block_size;
    if (block_size % 2 == 0) block_size++;
    if (block_size < 3) block_size = 3;

    cv::adaptiveThreshold(
        blurred, binary, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        block_size,
        params.adaptive_C
    );

    return binary;
}

void refine_mask(cv::Mat& binary, int kernel_size) {
    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(kernel_size, kernel_size)
    );

    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 2);
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN,  kernel, cv::Point(-1, -1), 1);

    cv::Mat fill_kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(kernel_size * 2 + 1, kernel_size * 2 + 1)
    );
    cv::dilate(binary, binary, fill_kernel, cv::Point(-1, -1), 1);
    cv::erode(binary, binary, fill_kernel, cv::Point(-1, -1), 1);
}

std::vector<std::vector<cv::Point>> filter_contours(
    const std::vector<std::vector<cv::Point>>& contours,
    const cv::Size& img_size,
    const DetectorParams& params)
{
    double total_area = static_cast<double>(img_size.width * img_size.height);
    double min_area = total_area * params.min_area_ratio;
    double max_area = total_area * params.max_area_ratio;

    std::vector<std::vector<cv::Point>> filtered;

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < min_area || area > max_area)
            continue;

        cv::Rect bbox = cv::boundingRect(contour);
        double aspect = static_cast<double>(bbox.width) / std::max(bbox.height, 1);
        if (aspect < params.aspect_ratio_min || aspect > params.aspect_ratio_max)
            continue;

        filtered.push_back(contour);
    }

    return filtered;
}

} // anonymous namespace


void compute_temperature_stats(
    const cv::Mat& temp_matrix,
    const cv::Mat& mask,
    double& max_val,
    double& min_val,
    double& avg_val,
    cv::Point& max_loc)
{
    if (temp_matrix.empty() || mask.empty()) {
        max_val = min_val = avg_val = 0.0;
        max_loc = cv::Point(-1, -1);
        return;
    }

    cv::Mat temp64;
    if (temp_matrix.type() != CV_64FC1)
        temp_matrix.convertTo(temp64, CV_64FC1);
    else
        temp64 = temp_matrix;

    cv::Point min_loc_unused;
    cv::minMaxLoc(temp64, &min_val, &max_val, &min_loc_unused, &max_loc, mask);
    avg_val = cv::mean(temp64, mask)[0];
}


DetectionResult detect_human_thermal(
    const cv::Mat& thermal_image,
    const DetectorParams& params)
{
    if (thermal_image.empty())
        throw std::invalid_argument("Input thermal_image is empty.");

    cv::Mat gray8 = normalize_to_8u(thermal_image);

    cv::Mat binary;
    if (params.use_adaptive_thresh) {
        binary = segment_adaptive(gray8, params);
    } else {
        double otsu_thresh = cv::threshold(gray8, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        (void)otsu_thresh;
    }

    refine_mask(binary, params.morph_kernel_size);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    auto valid = filter_contours(contours, thermal_image.size(), params);

    DetectionResult result;
    result.full_mask = cv::Mat::zeros(thermal_image.size(), CV_8UC1);

    for (const auto& contour : valid) {
        cv::drawContours(result.full_mask, std::vector<std::vector<cv::Point>>{contour},
                         0, cv::Scalar(255), cv::FILLED);

        HumanRegion region;
        region.bounding_box = cv::boundingRect(contour);
        region.area = cv::contourArea(contour);

        region.mask = cv::Mat::zeros(region.bounding_box.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> shifted_contour(1);
        for (const auto& pt : contour) {
            shifted_contour[0].push_back(
                cv::Point(pt.x - region.bounding_box.x, pt.y - region.bounding_box.y));
        }
        cv::drawContours(region.mask, shifted_contour, 0, cv::Scalar(255), cv::FILLED);

        cv::Mat roi_gray = gray8(region.bounding_box);
        double mn, mx;
        cv::Point max_loc_local;
        cv::minMaxLoc(roi_gray, &mn, &mx, nullptr, &max_loc_local, region.mask);
        region.min_temp = mn;
        region.max_temp = mx;
        region.avg_temp = cv::mean(roi_gray, region.mask)[0];
        region.max_temp_loc = cv::Point(
            max_loc_local.x + region.bounding_box.x,
            max_loc_local.y + region.bounding_box.y);

        result.regions.push_back(std::move(region));
    }

    return result;
}


DetectionResult detect_human_thermal(
    const cv::Mat& thermal_image,
    const cv::Mat& temp_matrix,
    const DetectorParams& params)
{
    if (thermal_image.empty())
        throw std::invalid_argument("Input thermal_image is empty.");
    if (temp_matrix.empty())
        throw std::invalid_argument("Input temp_matrix is empty.");
    if (thermal_image.size() != temp_matrix.size())
        throw std::invalid_argument("thermal_image and temp_matrix must have the same size.");

    cv::Mat binary = segment_by_temperature(
        temp_matrix, params.temp_threshold_low, params.temp_threshold_high);

    cv::Mat gray8 = normalize_to_8u(thermal_image);

    if (params.use_adaptive_thresh) {
        cv::Mat adaptive_mask = segment_adaptive(gray8, params);
        cv::bitwise_and(binary, adaptive_mask, binary);
    }

    refine_mask(binary, params.morph_kernel_size);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    auto valid = filter_contours(contours, thermal_image.size(), params);

    DetectionResult result;
    result.full_mask = cv::Mat::zeros(thermal_image.size(), CV_8UC1);

    for (const auto& contour : valid) {
        cv::drawContours(result.full_mask, std::vector<std::vector<cv::Point>>{contour},
                         0, cv::Scalar(255), cv::FILLED);

        HumanRegion region;
        region.bounding_box = cv::boundingRect(contour);
        region.area = cv::contourArea(contour);

        region.mask = cv::Mat::zeros(region.bounding_box.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> shifted_contour(1);
        for (const auto& pt : contour) {
            shifted_contour[0].push_back(
                cv::Point(pt.x - region.bounding_box.x, pt.y - region.bounding_box.y));
        }
        cv::drawContours(region.mask, shifted_contour, 0, cv::Scalar(255), cv::FILLED);

        cv::Mat roi_temp = temp_matrix(region.bounding_box);
        compute_temperature_stats(roi_temp, region.mask,
                                  region.max_temp, region.min_temp, region.avg_temp,
                                  region.max_temp_loc);
        region.max_temp_loc.x += region.bounding_box.x;
        region.max_temp_loc.y += region.bounding_box.y;

        result.regions.push_back(std::move(region));
    }

    return result;
}


cv::Mat visualize_detection(
    const cv::Mat& thermal_image,
    const DetectionResult& result)
{
    cv::Mat gray8 = normalize_to_8u(thermal_image);

    cv::Mat color;
    cv::applyColorMap(gray8, color, cv::COLORMAP_JET);

    cv::Mat overlay = color.clone();
    cv::Mat mask_3ch;
    cv::cvtColor(result.full_mask, mask_3ch, cv::COLOR_GRAY2BGR);
    overlay.setTo(cv::Scalar(0, 255, 0), result.full_mask);
    cv::addWeighted(color, 0.7, overlay, 0.3, 0, color);

    for (size_t i = 0; i < result.regions.size(); ++i) {
        const auto& region = result.regions[i];
        cv::rectangle(color, region.bounding_box, cv::Scalar(0, 255, 255), 2);

        std::string label = "P" + std::to_string(i) +
                            " Max:" + std::to_string(region.max_temp).substr(0, 5) +
                            " Avg:" + std::to_string(region.avg_temp).substr(0, 5);
        int baseline = 0;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::Point text_org(region.bounding_box.x,
                           std::max(region.bounding_box.y - 5, text_size.height + 5));
        cv::putText(color, label, text_org, cv::FONT_HERSHEY_SIMPLEX,
                    0.5, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);

        cv::drawMarker(color, region.max_temp_loc, cv::Scalar(0, 0, 255),
                       cv::MARKER_CROSS, 10, 2);
    }

    return color;
}

} // namespace thermal
