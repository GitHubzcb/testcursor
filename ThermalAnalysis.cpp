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
//  改动：新增 facings 参数（与 allImages 一一对应），
//        在每张图标题中标注"正面 / 背面 / 未知朝向"，
//        轴对称区域名称根据朝向动态映射（背面→背部/腰背部等）。
// ===================================================================
std::wstring BuildLLMPrompt(
    const std::vector<std::map<int, RegionTempStat>>& allImages,
    const std::vector<BodyFacing>& facings          // 与 allImages 等长
)
{
    std::wstring prompt;

    // ---- 角色 ----
    prompt += L"你是一名临床医学红外热成像分析专家。\n";
    prompt += L"必须用中文输出确定内容诊断报告，不需要分析过程。\n\n";

    // ---- 数据 ----
    prompt += L"【数据】\n";

    for (size_t imgIdx = 0; imgIdx < allImages.size(); imgIdx++)
    {
        const auto& stats = allImages[imgIdx];

        // 取当前图像的朝向（若 facings 长度不足则默认 UNKNOWN）
        BodyFacing facing = (imgIdx < facings.size())
            ? facings[imgIdx]
            : FACING_UNKNOWN;

        // 图像标题 + 朝向标注
        wchar_t imgHeader[128];
        swprintf_s(imgHeader, sizeof(imgHeader) / sizeof(wchar_t),
            L"图像%zu（%ls）：\n",
            imgIdx + 1,
            BodyFacingName[facing]);
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
                {
                    swprintf_s(diffBuf, sizeof(diffBuf) / sizeof(wchar_t),
                        L"  差值：%.1f℃  ⚠ 左右温差偏大，建议重点关注\n", diff);
                }
                else
                {
                    swprintf_s(diffBuf, sizeof(diffBuf) / sizeof(wchar_t),
                        L"  差值：%.1f℃\n", diff);
                }
                prompt += diffBuf;
            }
        }

        // ---- 轴对称区域（名称随朝向动态映射）----
        for (const auto& sr : kSingleRegions)
        {
            auto it = stats.find(sr.first);
            if (it == stats.end() || it->second.pixelCount == 0) continue;

            const RegionTempStat& s = it->second;
            // 根据朝向选择区域名称
            const wchar_t* regionLabel = GetRegionName(sr.first, facing);

            wchar_t buf[256];
            swprintf_s(buf, sizeof(buf) / sizeof(wchar_t),
                L"  %ls：均值 %.1f℃  最高 %.1f℃  最低 %.1f℃  标准差 %.2f℃\n",
                regionLabel,
                s.meanTemp, s.maxTemp, s.minTemp, s.stdDev);
            prompt += buf;
        }

        prompt += L"\n";
    }

    // ---- 是否含背面图像，追加背面专项说明 ----
    bool hasBack = false;
    for (auto f : facings)
        if (f == FACING_BACK) { hasBack = true; break; }

    if (hasBack)
    {
        prompt +=
            L"【背面图像说明】\n"
            L"背面图像中"背部"对应正面的胸腔区域，"腰背部"对应正面的腹部，\n"
            L""下背/臀部"对应正面的腰部，请结合临床背部热成像规律分析。\n\n";
    }

    // ---- 输出要求 ----
    prompt +=
        L"\n【输出要求】\n"
        L"1. 必须完整输出以下三个部分：\n"
        L"   【数据摘要】\n"
        L"   【各区域分析】\n"
        L"   【最终结论】\n"
        L"\n"
        L"2. 【各区域分析】格式示例：\n"
        L"   大腿：左 35.4℃ / 右 36.2℃，差值 0.8℃，轻度不对称，建议随访\n"
        L"\n"
        L"3. 异常判断规则：\n"
        L"   - 左右均值差 ≥ 0.8℃：提示温度不对称，可能存在炎症或血液循环异常\n"
        L"   - 区域均值超过正常体表温度（>37℃）：提示局部高温\n"
        L"   - 区域均值低于 28℃：提示局部低温或血液循环不足\n"
        L"   - 标准差 ≥ 1.5℃：提示区域内温度分布不均匀\n"
        L"\n"
        L"4. 如无明显异常，写"整体无明显异常"\n"
        L"5. 禁止输出多余分析、说明或编程内容\n"
        L"6. 无需输出思考过程，直接出结果\n";

    return prompt;
}
