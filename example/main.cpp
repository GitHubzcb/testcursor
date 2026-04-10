#include "thermal_human_detector.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <iostream>
#include <string>

static void print_usage(const char* prog) {
    std::cerr << "用法: " << prog << " <远红外图像路径> [温度矩阵路径.yml]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "参数:" << std::endl;
    std::cerr << "  远红外图像路径    灰度远红外图像 (8-bit 或 16-bit PNG/TIFF)" << std::endl;
    std::cerr << "  温度矩阵路径.yml  可选，OpenCV FileStorage 格式的温度矩阵" << std::endl;
    std::cerr << std::endl;
    std::cerr << "示例:" << std::endl;
    std::cerr << "  " << prog << " thermal.png" << std::endl;
    std::cerr << "  " << prog << " thermal.png temperature.yml" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string image_path = argv[1];
    cv::Mat thermal_image = cv::imread(image_path, cv::IMREAD_ANYDEPTH | cv::IMREAD_GRAYSCALE);
    if (thermal_image.empty()) {
        std::cerr << "[错误] 无法加载图像: " << image_path << std::endl;
        return 1;
    }

    std::cout << "已加载远红外图像: " << image_path
              << " (" << thermal_image.cols << "x" << thermal_image.rows
              << ", type=" << thermal_image.type() << ")" << std::endl;

    thermal::DetectorParams params;
    thermal::DetectionResult result;

    if (argc >= 3) {
        std::string temp_path = argv[2];
        cv::FileStorage fs(temp_path, cv::FileStorage::READ);
        if (!fs.isOpened()) {
            std::cerr << "[错误] 无法加载温度矩阵: " << temp_path << std::endl;
            return 1;
        }
        cv::Mat temp_matrix;
        fs["temperature"] >> temp_matrix;
        fs.release();

        if (temp_matrix.empty()) {
            std::cerr << "[错误] 温度矩阵为空 (键名应为 'temperature')" << std::endl;
            return 1;
        }

        std::cout << "已加载温度矩阵: " << temp_path << std::endl;
        result = thermal::detect_human_thermal(thermal_image, temp_matrix, params);
    } else {
        result = thermal::detect_human_thermal(thermal_image, params);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "检测到 " << result.regions.size() << " 个人体区域" << std::endl;
    std::cout << "========================================" << std::endl;

    for (size_t i = 0; i < result.regions.size(); ++i) {
        const auto& r = result.regions[i];
        std::cout << "[区域 " << i << "]" << std::endl;
        std::cout << "  包围矩形: (" << r.bounding_box.x << ", " << r.bounding_box.y
                  << ", " << r.bounding_box.width << "x" << r.bounding_box.height << ")" << std::endl;
        std::cout << "  面积: " << r.area << " px" << std::endl;
        std::cout << "  最高温度: " << r.max_temp << std::endl;
        std::cout << "  最低温度: " << r.min_temp << std::endl;
        std::cout << "  平均温度: " << r.avg_temp << std::endl;
        std::cout << "  最高温度位置: (" << r.max_temp_loc.x << ", " << r.max_temp_loc.y << ")" << std::endl;
        std::cout << std::endl;
    }

    cv::imwrite("human_mask.png", result.full_mask);
    std::cout << "人体 mask 已保存至: human_mask.png" << std::endl;

    cv::Mat vis = thermal::visualize_detection(thermal_image, result);
    cv::imwrite("detection_result.png", vis);
    std::cout << "可视化结果已保存至: detection_result.png" << std::endl;

    return 0;
}
