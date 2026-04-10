#ifndef THERMAL_HUMAN_DETECTOR_H
#define THERMAL_HUMAN_DETECTOR_H

#include <opencv2/core.hpp>
#include <vector>
#include <string>

/**
 * 远红外（热成像）图像人体检测与温度统计工具
 *
 * 工作流程：
 *   1. 输入远红外灰度图（单通道，像素值对应辐射强度或已映射温度）
 *   2. 通过自适应阈值 + 形态学 + 轮廓分析提取疑似人体热区
 *   3. 输出二值 mask（人体区域=255，背景=0）
 *   4. 可结合温度矩阵对人体区域做温度统计（最大/最小/平均温度）
 */

namespace thermal {

struct DetectorParams {
    double temp_threshold_low  = 30.0;   // 人体温度下限 (°C)
    double temp_threshold_high = 42.0;   // 人体温度上限 (°C)
    int    morph_kernel_size   = 5;      // 形态学核大小
    double min_area_ratio      = 0.002;  // 最小区域占图像面积比
    double max_area_ratio      = 0.8;    // 最大区域占图像面积比
    double aspect_ratio_min    = 0.2;    // 最小宽高比
    double aspect_ratio_max    = 3.0;    // 最大宽高比
    bool   use_adaptive_thresh = true;   // 是否使用自适应阈值（当无温度矩阵时）
    int    adaptive_block_size = 35;     // 自适应阈值块大小
    double adaptive_C          = -8.0;   // 自适应阈值常数
};

struct HumanRegion {
    cv::Rect bounding_box;      // 包围矩形
    cv::Mat  mask;              // 该区域的局部 mask（与 bounding_box 等大）
    double   area;              // 区域像素面积
    double   max_temp;          // 区域内最高温度
    double   min_temp;          // 区域内最低温度
    double   avg_temp;          // 区域内平均温度
    cv::Point max_temp_loc;     // 最高温度位置（相对于原图）
};

struct DetectionResult {
    cv::Mat                    full_mask;  // 与原图等大的完整人体 mask
    std::vector<HumanRegion>   regions;    // 各人体区域信息
};

/**
 * @brief 从远红外图像中检测人体区域并生成 mask
 *
 * @param thermal_image  输入远红外灰度图（CV_8UC1 或 CV_16UC1），
 *                       像素值代表灰度/辐射强度
 * @param params         检测参数
 * @return DetectionResult 检测结果，包含 mask 和各区域统计信息
 *
 * @note 此重载不含温度矩阵，温度统计字段将基于灰度值填充
 */
DetectionResult detect_human_thermal(
    const cv::Mat& thermal_image,
    const DetectorParams& params = DetectorParams()
);

/**
 * @brief 从远红外图像中检测人体区域并结合温度矩阵进行统计
 *
 * @param thermal_image  输入远红外灰度图（CV_8UC1 或 CV_16UC1）
 * @param temp_matrix    与 thermal_image 等大的温度矩阵（CV_32FC1 或 CV_64FC1），
 *                       每个像素对应实际温度值（单位 °C）
 * @param params         检测参数
 * @return DetectionResult 检测结果，包含 mask 和各区域的真实温度统计
 */
DetectionResult detect_human_thermal(
    const cv::Mat& thermal_image,
    const cv::Mat& temp_matrix,
    const DetectorParams& params = DetectorParams()
);

/**
 * @brief 根据 mask 对温度矩阵进行温度统计
 *
 * @param temp_matrix  温度矩阵（CV_32FC1 或 CV_64FC1）
 * @param mask         二值 mask（CV_8UC1，非零区域参与统计）
 * @param[out] max_val 最大温度
 * @param[out] min_val 最小温度
 * @param[out] avg_val 平均温度
 * @param[out] max_loc 最大温度位置
 */
void compute_temperature_stats(
    const cv::Mat& temp_matrix,
    const cv::Mat& mask,
    double& max_val,
    double& min_val,
    double& avg_val,
    cv::Point& max_loc
);

/**
 * @brief 可视化检测结果（用于调试）
 *
 * @param thermal_image  原始远红外图
 * @param result         检测结果
 * @return cv::Mat       可视化后的彩色图像
 */
cv::Mat visualize_detection(
    const cv::Mat& thermal_image,
    const DetectionResult& result
);

} // namespace thermal

#endif // THERMAL_HUMAN_DETECTOR_H
