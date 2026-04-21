// ===================================================================
//  ThermalAnalysis.cpp
//  红外热成像人体区域分析实现
//  新增功能：
//    1. 区域左右分侧（大腿、小腿、膝盖、脚、胳膊、手）
//    2. 温度统计新增标准差
//    3. 提示词构建输出左右、差值、最高、最低、标准差
//    4. 正背面朝向检测（DetectBodyFacing）
// ===================================================================

#include "ThermalAnalysis.h"
#include <algorithm>
#include <cmath>
#include <sstream>

// ===================================================================
//  DetectBodyFacing
//
//  基于 COCO-17 面部关键点置信度进行加权投票，判断人体朝向。
//
//  核心原理：
//    正面朝相机时：鼻子、双眼可见，耳朵置信度较低（被面颊遮挡）。
//    背面朝相机时：鼻子/眼睛消失（或置信度极低），耳朵轮廓反而可见。
//    侧身：介于两者之间，单侧眼/耳可见。
//
//  加权得分规则（score > 0 → 正面，score < 0 → 背面）：
//    +3.0  鼻子置信度  > 0.5
//    +2.0  至少一只眼  > 0.4
//    +1.0  双眼平均置信度 > 耳朵平均置信度
//    -2.0  鼻子置信度  < 0.15（正面特征消失）
//    -2.0  耳朵可见（≥0.4）且眼睛均不可见（<0.3）
//    -1.5  耳朵平均置信度 > 眼睛平均置信度 + 0.2
//    -1.0  耳朵总置信度 > 面部（鼻+眼）总置信度
//
//  判决阈值：
//    score ≥  1.5 → FACING_FRONT
//    score ≤ -1.0 → FACING_BACK
//    其余          → FACING_UNKNOWN（侧身或关键点严重缺失）
// ===================================================================
BodyFacing DetectBodyFacing(const PosePerson& pose)
{
    const auto& kp = pose.keypoints;

    float confNose  = kp[NOSE].conf;
    float confLEye  = kp[L_EYE].conf;
    float confREye  = kp[R_EYE].conf;
    float confLEar  = kp[L_EAR].conf;
    float confREar  = kp[R_EAR].conf;

    float eyeAvg = (confLEye + confREye) * 0.5f;
    float earAvg = (confLEar + confREar) * 0.5f;

    float facialSum = confNose + confLEye + confREye;
    float earSum    = confLEar + confREar;

    bool noseVisible = confNose  > 0.5f;
    bool anyEyeVis   = confLEye  > 0.4f || confREye > 0.4f;
    bool anyEarVis   = confLEar  > 0.4f || confREar > 0.4f;
    bool eyesHidden  = confLEye  < 0.3f && confREye < 0.3f;
    bool noseMissing = confNose  < 0.15f;

    float score = 0.f;

    // ---- 正面证据 ----
    if (noseVisible)  score += 3.0f;
    if (anyEyeVis)    score += 2.0f;
    if (eyeAvg > earAvg) score += 1.0f;

    // ---- 背面证据 ----
    if (noseMissing)              score -= 2.0f;
    if (anyEarVis && eyesHidden)  score -= 2.0f;
    if (earAvg > eyeAvg + 0.2f)  score -= 1.5f;
    if (earSum > facialSum)       score -= 1.0f;

    if (score >= 1.5f)  return FACING_FRONT;
    if (score <= -1.0f) return FACING_BACK;
    return FACING_UNKNOWN;
}

// ===================================================================
//  BuildBodyRegionMask
//  核心改动：
//    1. 利用人体中线将腿部等区域分配到左/右独立 ID
//    2. 自动检测正/背面（DetectBodyFacing），通过 outFacing 输出
//       - 正面：CHEST=胸部，ABDOMEN=腹部（默认命名）
//       - 背面：同样使用 CHEST/ABDOMEN 等区域 ID，但在显示层
//               通过 GetRegionName(region, facing) 映射为"背部/腰背部"
//         这样区域 ID 不变，温度统计逻辑不受影响；
//         只有名称和提示词层感知朝向。
//
//  注意：COCO 关键点坐标系中，"左"关键点（L_HIP 等）在图像坐标中
//  对应画面右侧（镜像），因此需要通过 x 坐标而非名称判断图像左右。
// ===================================================================
cv::Mat BuildBodyRegionMask(
    const PosePerson& pose,
    int imgW,
    int imgH,
    BodyFacing& outFacing   // 输出：检测到的正背面朝向
)
{
    cv::Mat mask(imgH, imgW, CV_8UC1, cv::Scalar(REGION_UNKNOWN));

    // 首先检测正背面朝向
    outFacing = DetectBodyFacing(pose);

    auto KP = [&](int id) -> cv::Point2f { return pose.keypoints[id].pt; };
    auto OK = [&](int id) -> bool        { return pose.keypoints[id].conf > 0.4f; };

    BodyVisibleType type = DetectBodyType(pose);
    if (type == UNKNOWN_BODY) return mask;

    // ------------------------------------------------------------------
    //  计算人体中线 x（画面坐标）
    //  优先用双髋，次用双肩，再退化到图像中心
    // ------------------------------------------------------------------
    float centerX = imgW * 0.5f;
    if (OK(L_HIP) && OK(R_HIP))
        centerX = (KP(L_HIP).x + KP(R_HIP).x) * 0.5f;
    else if (OK(L_SHOULDER) && OK(R_SHOULDER))
        centerX = (KP(L_SHOULDER).x + KP(R_SHOULDER).x) * 0.5f;

    // 为每行计算一个动态中线 x，使分割边界随身体倾斜自然弯曲。
    // 用线性插值：上端取肩中心 x，下端取踝中心 x。
    float topCX  = centerX;
    float botCX  = centerX;

    if (OK(L_SHOULDER) && OK(R_SHOULDER))
        topCX = (KP(L_SHOULDER).x + KP(R_SHOULDER).x) * 0.5f;
    if (OK(L_ANKLE) && OK(R_ANKLE))
        botCX = (KP(L_ANKLE).x + KP(R_ANKLE).x) * 0.5f;
    else if (OK(L_KNEE) && OK(R_KNEE))
        botCX = (KP(L_KNEE).x + KP(R_KNEE).x) * 0.5f;

    // ------------------------------------------------------------------
    //  辅助 lambda：给定行 y，返回该行的中线 x
    // ------------------------------------------------------------------
    auto lineCX = [&](int y) -> float
    {
        if (imgH <= 1) return centerX;
        float t = (float)y / (imgH - 1);
        return topCX + t * (botCX - topCX);
    };

    // ------------------------------------------------------------------
    //  各关键 y 坐标（按体型类型分情况取）
    // ------------------------------------------------------------------
    if (type == FULL_BODY)
    {
        float y_shoulder = (KP(L_SHOULDER).y + KP(R_SHOULDER).y) * 0.5f;
        float y_hip      = (KP(L_HIP).y      + KP(R_HIP).y)      * 0.5f;
        float y_mid      = (y_shoulder + y_hip) * 0.5f;

        float y_knee = (OK(L_KNEE) && OK(R_KNEE)) ?
            (KP(L_KNEE).y + KP(R_KNEE).y) * 0.5f :
            y_hip + 120;

        float y_ankle = (OK(L_ANKLE) && OK(R_ANKLE)) ?
            (KP(L_ANKLE).y + KP(R_ANKLE).y) * 0.5f :
            y_knee + 120;

        // 膝盖窗口高度（膝关节上下各一半）
        float kneeHalf = std::max(10.f, (y_ankle - y_knee) * 0.12f);

        for (int y = 0; y < imgH; ++y)
        {
            float cx = lineCX(y);
            if (y < y_shoulder)
            {
                // 头颈胸 —— 轴对称，不分左右
                mask.row(y).setTo(REGION_CHEST);
            }
            else if (y < y_mid)
            {
                mask.row(y).setTo(REGION_ABDOMEN);
            }
            else if (y < y_hip)
            {
                mask.row(y).setTo(REGION_WAIST);
            }
            else if (y < y_knee - kneeHalf)
            {
                // 大腿：按像素 x 与中线比较
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_THIGH_L : REGION_THIGH_R;
            }
            else if (y < y_knee + kneeHalf)
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_KNEE_L : REGION_KNEE_R;
            }
            else if (y < y_ankle)
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_LEG_L : REGION_LEG_R;
            }
            else
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_FOOT_L : REGION_FOOT_R;
            }
        }
    }
    else if (type == LOWER_BODY)
    {
        float y_top =
            (OK(L_HIP) && OK(R_HIP)) ?
            (KP(L_HIP).y + KP(R_HIP).y) * 0.5f :
            (OK(L_KNEE) ? KP(L_KNEE).y - 120 : imgH * 0.3f);

        float y_knee =
            (OK(L_KNEE) && OK(R_KNEE)) ?
            (KP(L_KNEE).y + KP(R_KNEE).y) * 0.5f :
            y_top + 120;

        float y_ankle =
            (OK(L_ANKLE) && OK(R_ANKLE)) ?
            (KP(L_ANKLE).y + KP(R_ANKLE).y) * 0.5f :
            y_knee + 120;

        float kneeHalf = std::max(10.f, (y_ankle - y_knee) * 0.12f);

        for (int y = 0; y < imgH; ++y)
        {
            float cx = lineCX(y);
            if (y < y_top)
            {
                mask.row(y).setTo(REGION_UNKNOWN);
            }
            else if (y < y_knee - kneeHalf)
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_THIGH_L : REGION_THIGH_R;
            }
            else if (y < y_knee + kneeHalf)
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_KNEE_L : REGION_KNEE_R;
            }
            else if (y < y_ankle)
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_LEG_L : REGION_LEG_R;
            }
            else
            {
                for (int x = 0; x < imgW; ++x)
                    mask.at<uchar>(y, x) = (x < cx) ? REGION_FOOT_L : REGION_FOOT_R;
            }
        }
    }
    else if (type == UPPER_BODY)
    {
        float y_shoulder = (KP(L_SHOULDER).y + KP(R_SHOULDER).y) * 0.5f;
        float y_bottom   = y_shoulder + 200;

        for (int y = 0; y < imgH; ++y)
        {
            if (y < y_shoulder)
                mask.row(y).setTo(REGION_NECK);
            else if (y < y_bottom)
                mask.row(y).setTo(REGION_CHEST);
            else
                mask.row(y).setTo(REGION_UNKNOWN);
        }
    }

    // ------------------------------------------------------------------
    //  手臂 / 手：沿骨骼线段绘制（宽度 30px），左右分侧
    //  判断依据：骨骼中点 x 与 centerX 比较
    // ------------------------------------------------------------------
    auto drawLimbLR = [&](int s, int e, uchar rL, uchar rR)
    {
        if (!OK(s) || !OK(e)) return;
        float mx = (KP(s).x + KP(e).x) * 0.5f;
        uchar r  = (mx < centerX) ? rL : rR;
        cv::line(mask, KP(s), KP(e), r, 30);
    };

    drawLimbLR(L_SHOULDER, L_ELBOW, REGION_ARM_L, REGION_ARM_R);
    drawLimbLR(R_SHOULDER, R_ELBOW, REGION_ARM_L, REGION_ARM_R);
    drawLimbLR(L_ELBOW,    L_WRIST, REGION_ARM_L, REGION_ARM_R);
    drawLimbLR(R_ELBOW,    R_WRIST, REGION_ARM_L, REGION_ARM_R);

    // 手部（腕关键点周围圆形区域）
    auto drawHandLR = [&](int wrist, uchar rL, uchar rR)
    {
        if (!OK(wrist)) return;
        uchar r = (KP(wrist).x < centerX) ? rL : rR;
        cv::circle(mask, KP(wrist), 20, r, -1);
    };

    drawHandLR(L_WRIST, REGION_HAND_L, REGION_HAND_R);
    drawHandLR(R_WRIST, REGION_HAND_L, REGION_HAND_R);

    return mask;
}

// ===================================================================
//  CalcRegionTempFromMatrix
//  改动：使用 RegionTempStat::Accumulate 在线累积，
//        最后调用 Finalize 计算均值和标准差。
// ===================================================================
std::map<int, RegionTempStat> CalcRegionTempFromMatrix(
    const cv::Mat& tempMat,     // CV_32F
    const cv::Mat& regionMask,  // CV_8UC1
    const cv::Mat& humanMask    // CV_8UC1（可为空）
)
{
    std::map<int, RegionTempStat> stats;

    for (int y = 0; y < (tempMat.rows - 1); y++)
    {
        for (int x = 0; x < tempMat.cols; x++)
        {
            if (!humanMask.empty() && humanMask.at<uchar>(y, x) == 0)
                continue;

            int region = regionMask.at<uchar>(y, x);
            if (region == 0 || region == REGION_UNKNOWN) continue;

            float t = tempMat.at<float>(y, x);
            stats[region].Accumulate(t, x, y);
        }
    }

    for (auto& kv : stats)
        kv.second.Finalize();

    return stats;
}

// ===================================================================
//  BuildLLMPrompt
//  根据所有图像的统计数据，生成结构化、信息丰富的提示词。
//
//  输出格式（每个对称区域分左右，每侧输出均值、最高、最低、标准差，
//             对称区域间输出左右差值）：
//
//  大腿：
//    左：35.4℃
//    右：36.2℃
//    差值：0.8℃（⚠ 超过阈值，可能存在不对称）
//    最高：36.8℃（右侧）
//    最低：34.2℃（左侧）
//    标准差：左 0.6℃ / 右 0.4℃
// ===================================================================

// 区域是否有左右配对
struct RegionPair { BodyRegion left; BodyRegion right; const wchar_t* name; };

static const RegionPair kPairedRegions[] =
{
    { REGION_THIGH_L, REGION_THIGH_R, L"大腿" },
    { REGION_KNEE_L,  REGION_KNEE_R,  L"膝盖" },
    { REGION_LEG_L,   REGION_LEG_R,   L"小腿" },
    { REGION_FOOT_L,  REGION_FOOT_R,  L"脚"   },
    { REGION_ARM_L,   REGION_ARM_R,   L"胳膊" },
    { REGION_HAND_L,  REGION_HAND_R,  L"手"   },
};

// 单侧或轴对称区域
static const std::pair<BodyRegion, const wchar_t*> kSingleRegions[] =
{
    { REGION_HEAD,    L"头"   },
    { REGION_NECK,    L"颈部" },
    { REGION_CHEST,   L"胸部" },
    { REGION_ABDOMEN, L"腹部" },
    { REGION_WAIST,   L"腰部" },
};

// 左右差值告警阈值（℃）—— 超过此值在提示词中标注异常
static constexpr float kLRDiffWarn = 0.8f;

// 高温/低温告警阈值偏移（相对于均值的标准差倍数）
static constexpr float kHotWarnStdMul = 2.0f;

// ---------------------------------------------------------------
//  格式化单侧统计为宽字符串
// ---------------------------------------------------------------
static std::wstring FormatSideStat(
    const wchar_t* sideLabel,
    const RegionTempStat& s,
    bool showDetail = true
)
{
    wchar_t buf[512];
    if (showDetail)
    {
        swprintf_s(buf, sizeof(buf) / sizeof(wchar_t),
            L"  %ls：%.1f℃（最高 %.1f℃  最低 %.1f℃  标准差 %.2f℃）\n",
            sideLabel,
            s.meanTemp,
            s.maxTemp,
            s.minTemp,
            s.stdDev);
    }
    else
    {
        swprintf_s(buf, sizeof(buf) / sizeof(wchar_t),
            L"  %ls：%.1f℃\n",
            sideLabel,
            s.meanTemp);
    }
    return buf;
}

// ===================================================================
//  BuildLLMPrompt
//
//  采用"前缀补全"模式：提示词最后直接以报告第一行【数据摘要】结尾，
//  利用 llama.cpp /completion 的纯续写特性让模型从该位置接着生成，
//  彻底避免模型将格式规则当成正文输出。
//
//  结构：
//    <简短角色声明（1行）>
//    <异常判断准则（内嵌在数据旁，用⚠标记）>
//    检测数据：
//      图像N（正面/背面）：
//        区域名：左/右均值℃（最高 最低 标准差）差值 [⚠]
//        ...
//    ---
//    根据以上数据，用中文写红外热成像诊断报告：
//    【数据摘要】
//  <---- 模型从这里续写 ---->
// ===================================================================
std::wstring BuildLLMPrompt(
    const std::vector<std::map<int, RegionTempStat>>& allImages,
    const std::vector<BodyFacing>& facings
)
{
    std::wstring prompt;

    // ---- 角色声明（极简，不含任何"输出要求"列表）----
    prompt += L"你是临床医学红外热成像分析专家。"
              L"左右差≥0.8℃或均值>37℃或<28℃或标准差≥1.5℃视为异常。\n\n";

    // ---- 检测数据 ----
    prompt += L"检测数据：\n";

    for (size_t imgIdx = 0; imgIdx < allImages.size(); imgIdx++)
    {
        const auto& stats = allImages[imgIdx];
        BodyFacing facing = (imgIdx < facings.size())
            ? facings[imgIdx] : FACING_UNKNOWN;

        wchar_t imgHeader[128];
        swprintf_s(imgHeader, sizeof(imgHeader) / sizeof(wchar_t),
            L"图像%zu（%ls）：\n", imgIdx + 1, BodyFacingName[facing]);
        prompt += imgHeader;

        // ---- 对称配对区域（左右分侧）----
        for (const auto& pair : kPairedRegions)
        {
            auto itL = stats.find(pair.left);
            auto itR = stats.find(pair.right);
            bool hasL = (itL != stats.end() && itL->second.pixelCount > 0);
            bool hasR = (itR != stats.end() && itR->second.pixelCount > 0);
            if (!hasL && !hasR) continue;

            prompt += std::wstring(L"  ") + pair.name + L"：\n";

            if (hasL) prompt += FormatSideStat(L"左", itL->second);
            if (hasR) prompt += FormatSideStat(L"右", itR->second);

            if (hasL && hasR)
            {
                float diff = std::fabs(itL->second.meanTemp - itR->second.meanTemp);
                wchar_t diffBuf[128];
                if (diff >= kLRDiffWarn)
                    swprintf_s(diffBuf, sizeof(diffBuf) / sizeof(wchar_t),
                        L"  差值：%.1f℃ ⚠\n", diff);
                else
                    swprintf_s(diffBuf, sizeof(diffBuf) / sizeof(wchar_t),
                        L"  差值：%.1f℃\n", diff);
                prompt += diffBuf;
            }
        }

        // ---- 轴对称区域 ----
        for (const auto& sr : kSingleRegions)
        {
            auto it = stats.find(sr.first);
            if (it == stats.end() || it->second.pixelCount == 0) continue;

            const RegionTempStat& s = it->second;
            const wchar_t* regionLabel = GetRegionName(sr.first, facing);

            // 判断是否有异常标记
            bool hot     = s.meanTemp > 37.0f;
            bool cold    = s.meanTemp < 28.0f;
            bool uneven  = s.stdDev   >= 1.5f;
            const wchar_t* warn = (hot || cold || uneven) ? L" ⚠" : L"";

            wchar_t buf[256];
            swprintf_s(buf, sizeof(buf) / sizeof(wchar_t),
                L"  %ls：均值 %.1f℃  最高 %.1f℃  最低 %.1f℃  标准差 %.2f℃%ls\n",
                regionLabel, s.meanTemp, s.maxTemp, s.minTemp, s.stdDev, warn);
            prompt += buf;
        }

        // 背面时追加一行简短提示（不用长段落，避免模型回显）
        if (facing == FACING_BACK)
            prompt += L"  （背面：背部=胸腔对应区，腰背部=腹部对应区）\n";

        prompt += L"\n";
    }

    // ---- 前缀补全触发词：提示词最后以报告第一行结束 ----
    // 模型看到这里会直接续写报告正文，不会重复任何上文内容。
    prompt +=
        L"根据以上检测数据，写中文红外热成像诊断报告：\n"
        L"【数据摘要】\n";

    return prompt;
}
