#pragma once
// ===================================================================
//  ThermalAnalysis.h
//  红外热成像人体区域分析 —— 支持左右分侧、标准差统计
// ===================================================================

#include <opencv2/opencv.hpp>
#include <vector>
#include <map>
#include <string>
#include <cfloat>
#include <cmath>

// ---------------------------------------------------------------
//  COCO-17 关键点索引
// ---------------------------------------------------------------
enum PoseKpt
{
    NOSE = 0,
    L_EYE, R_EYE,
    L_EAR, R_EAR,
    L_SHOULDER, R_SHOULDER,
    L_ELBOW, R_ELBOW,
    L_WRIST, R_WRIST,
    L_HIP, R_HIP,
    L_KNEE, R_KNEE,
    L_ANKLE, R_ANKLE
};

struct KeyPointPS
{
    cv::Point2f pt;
    float conf;
};

struct PosePerson
{
    std::vector<KeyPointPS> keypoints; // size = 17 (COCO)
    float score;
    cv::Rect bbox;
};

// ---------------------------------------------------------------
//  身体区域枚举
//  说明：带 _L / _R 后缀的为左右分侧区域；
//        无后缀的（HEAD / NECK / CHEST / ABDOMEN / WAIST）为轴对称中线区域，不分侧。
//  注意：编码连续，便于 mask 存储在 CV_8UC1（最多 255 个类别）。
// ---------------------------------------------------------------
enum BodyRegion
{
    REGION_BACKGROUND = 0,

    REGION_HEAD,
    REGION_NECK,
    REGION_CHEST,
    REGION_ABDOMEN,
    REGION_WAIST,

    // 大腿（左/右）
    REGION_THIGH_L,
    REGION_THIGH_R,

    // 膝盖（左/右）
    REGION_KNEE_L,
    REGION_KNEE_R,

    // 小腿（左/右）
    REGION_LEG_L,
    REGION_LEG_R,

    // 脚（左/右）
    REGION_FOOT_L,
    REGION_FOOT_R,

    // 胳膊（左/右）
    REGION_ARM_L,
    REGION_ARM_R,

    // 手（左/右）
    REGION_HAND_L,
    REGION_HAND_R,

    REGION_UNKNOWN,
    REGION_COUNT   // 总数，用于边界检查
};

// 区域显示名称（与枚举严格对应）
static const wchar_t* BodyRegionName[] =
{
    L"背景",           // REGION_BACKGROUND
    L"头",             // REGION_HEAD
    L"颈部",           // REGION_NECK
    L"胸部",           // REGION_CHEST
    L"腹部",           // REGION_ABDOMEN
    L"腰部",           // REGION_WAIST
    L"大腿(左)",       // REGION_THIGH_L
    L"大腿(右)",       // REGION_THIGH_R
    L"膝盖(左)",       // REGION_KNEE_L
    L"膝盖(右)",       // REGION_KNEE_R
    L"小腿(左)",       // REGION_LEG_L
    L"小腿(右)",       // REGION_LEG_R
    L"脚(左)",         // REGION_FOOT_L
    L"脚(右)",         // REGION_FOOT_R
    L"胳膊(左)",       // REGION_ARM_L
    L"胳膊(右)",       // REGION_ARM_R
    L"手(左)",         // REGION_HAND_L
    L"手(右)",         // REGION_HAND_R
    L"Unknown"         // REGION_UNKNOWN
};

// 区域绘制颜色（与枚举严格对应）
static const cv::Scalar RegionColor[] =
{
    cv::Scalar(0,   0,   0),     // BACKGROUND
    cv::Scalar(0,   0, 255),     // HEAD      - 红
    cv::Scalar(0, 128, 255),     // NECK      - 橙
    cv::Scalar(0, 255,   0),     // CHEST     - 绿
    cv::Scalar(255,255,   0),    // ABDOMEN   - 黄
    cv::Scalar(255,  0,   0),    // WAIST     - 蓝
    cv::Scalar(255,  0, 255),    // THIGH_L   - 紫
    cv::Scalar(200,  0, 200),    // THIGH_R   - 深紫
    cv::Scalar(0, 255, 255),     // KNEE_L    - 青
    cv::Scalar(0, 200, 200),     // KNEE_R    - 深青
    cv::Scalar(128,255, 128),    // LEG_L     - 浅绿
    cv::Scalar(64, 200,  64),    // LEG_R     - 深绿
    cv::Scalar(255,128, 128),    // FOOT_L    - 粉
    cv::Scalar(200, 64,  64),    // FOOT_R    - 深粉
    cv::Scalar(128,128, 255),    // ARM_L     - 浅蓝
    cv::Scalar(64,  64, 200),    // ARM_R     - 深蓝
    cv::Scalar(200,200, 200),    // HAND_L    - 浅灰
    cv::Scalar(128,128, 128),    // HAND_R    - 深灰
    cv::Scalar(50,  50,  50)     // UNKNOWN
};

// ---------------------------------------------------------------
//  温度统计结构体（支持标准差）
// ---------------------------------------------------------------
struct RegionTempStat
{
    float minTemp   = FLT_MAX;
    float maxTemp   = -FLT_MAX;
    float meanTemp  = 0.f;
    float stdDev    = 0.f;   // 标准差
    int   pixelCount = 0;

    cv::Point minPt;
    cv::Point maxPt;

    // 内部计算用（不对外暴露）
    double _sumTemp  = 0.0;
    double _sumSq    = 0.0;

    // 累积单个像素温度
    inline void Accumulate(float t, int x, int y)
    {
        if (t < minTemp) { minTemp = t; minPt = cv::Point(x, y); }
        if (t > maxTemp) { maxTemp = t; maxPt = cv::Point(x, y); }
        _sumTemp += t;
        _sumSq   += (double)t * t;
        pixelCount++;
    }

    // 统计完所有像素后调用
    void Finalize()
    {
        if (pixelCount > 0)
        {
            meanTemp = (float)(_sumTemp / pixelCount);
            double variance = _sumSq / pixelCount - (double)meanTemp * meanTemp;
            stdDev = (float)std::sqrt(std::max(0.0, variance));
        }
    }
};

// ---------------------------------------------------------------
//  体型判断
// ---------------------------------------------------------------
enum BodyVisibleType { FULL_BODY, UPPER_BODY, LOWER_BODY, UNKNOWN_BODY };

inline BodyVisibleType DetectBodyType(const PosePerson& pose)
{
    auto OK = [&](int id) { return pose.keypoints[id].conf > 0.4f; };

    bool hasShoulder = OK(L_SHOULDER) && OK(R_SHOULDER);
    bool hasHip      = OK(L_HIP)      && OK(R_HIP);
    bool hasKnee     = OK(L_KNEE)     || OK(R_KNEE);
    bool hasAnkle    = OK(L_ANKLE)    || OK(R_ANKLE);

    if (hasShoulder && hasHip) return FULL_BODY;
    if (hasShoulder)           return UPPER_BODY;
    if (hasHip || hasKnee || hasAnkle) return LOWER_BODY;
    return UNKNOWN_BODY;
}

// ---------------------------------------------------------------
//  正背面判断
//
//  算法：利用 COCO-17 面部关键点置信度进行加权投票。
//
//  正面证据（得分+）：
//    - 鼻子(NOSE)置信度高         → 最强正面特征，权重3
//    - 眼睛(L_EYE/R_EYE)可见      → 正面特征，权重2
//    - 面部关键点总置信度 > 耳朵   → 正面倾向，权重1
//
//  背面证据（得分-）：
//    - 鼻子置信度极低              → 正面特征消失，权重2
//    - 耳朵可见但眼睛不可见        → 背面特征，权重2
//    - 耳朵置信度明显高于眼睛      → 背面倾向，权重1.5
//    - 耳朵总置信度 > 面部总置信度 → 背面倾向，权重1
//
//  判决：score ≥ 1.5 → 正面；score ≤ -1.0 → 背面；否则 → 未知（侧身）
// ---------------------------------------------------------------
enum BodyFacing
{
    FACING_FRONT,    // 正面
    FACING_BACK,     // 背面
    FACING_UNKNOWN   // 无法判断（侧身或关键点缺失）
};

// 正/背面名称，用于提示词和日志
static const wchar_t* BodyFacingName[] =
{
    L"正面",
    L"背面",
    L"未知朝向"
};

BodyFacing DetectBodyFacing(const PosePerson& pose);

// ---------------------------------------------------------------
//  函数前向声明
// ---------------------------------------------------------------

// 构建身体区域 mask，同时输出正背面朝向
cv::Mat BuildBodyRegionMask(
    const PosePerson& pose,
    int imgW,
    int imgH,
    BodyFacing& outFacing
);

// 温度统计（含标准差）
std::map<int, RegionTempStat> CalcRegionTempFromMatrix(
    const cv::Mat& tempMat,
    const cv::Mat& regionMask,
    const cv::Mat& humanMask
);

// 构建 LLM 提示词（含左右、标准差、正背面朝向）
std::wstring BuildLLMPrompt(
    const std::vector<std::map<int, RegionTempStat>>& allImages,
    const std::vector<BodyFacing>& facings
);

// ---------------------------------------------------------------
//  根据朝向返回区域中文名
//  背面时 CHEST→背部、ABDOMEN→腰背部、WAIST→下背/臀部
//  NECK/HEAD 也区分前后命名，其余区域名称不变。
// ---------------------------------------------------------------
inline const wchar_t* GetRegionName(int region, BodyFacing facing)
{
    if (facing == FACING_BACK)
    {
        switch (region)
        {
        case REGION_HEAD:    return L"后脑";
        case REGION_NECK:    return L"颈后";
        case REGION_CHEST:   return L"背部";
        case REGION_ABDOMEN: return L"腰背部";
        case REGION_WAIST:   return L"下背/臀部";
        default: break;
        }
    }
    if (region >= 0 && region < REGION_COUNT)
        return BodyRegionName[region];
    return L"Unknown";
}
