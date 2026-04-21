// ===================================================================
//  OnBnClickedButtonAitrain_patch.cpp
//
//  本文件展示对 OnBnClickedButtonAitrain 函数中各处的修改片段。
//  请将以下代码替换到原函数对应位置。
//
//  本次新增改动（正背面检测）标记为 ★★
// ===================================================================

// ---------------------------------------------------------------
//  [修改1] 变量声明（函数开头）
//  新增 allFacings 列表，与 allImages 一一对应记录每张图的朝向。
//
//  旧：
//      std::vector<std::map<CStringW, double>> allImages;
//
//  新：
//      std::vector<std::map<int, RegionTempStat>> allImages;
//      std::vector<BodyFacing> allFacings;              // ★★ 新增
// ---------------------------------------------------------------


// ---------------------------------------------------------------
//  [修改2] 循环内：BuildBodyRegionMask 调用
//
//  旧：
//      cv::Mat bodyRegionMask = BuildBodyRegionMask(person, image.cols, image.rows);
//
//  新：
//      BodyFacing facing;                               // ★★ 新增
//      cv::Mat bodyRegionMask = BuildBodyRegionMask(person, image.cols, image.rows, facing);
// ---------------------------------------------------------------


// ---------------------------------------------------------------
//  [修改3] 循环内：可视化文字改用 GetRegionName
//  使背面图像显示"背部"而非"胸部"等正面名称。
//
//  旧：
//      text.Format("%s %.1fC(±%.1f)", CString(BodyRegionName[region]), ...);
//
//  新：
//      text.Format("%s %.1fC(±%.1f)", CString(GetRegionName(region, facing)), ...); // ★★
// ---------------------------------------------------------------


// ---------------------------------------------------------------
//  [修改4] 循环末尾：保存数据时同时保存朝向
//
//  旧：
//      allImages.push_back(regionStats);
//
//  新：
//      allImages.push_back(regionStats);
//      allFacings.push_back(facing);                    // ★★ 新增
// ---------------------------------------------------------------


// ---------------------------------------------------------------
//  [修改5] 提示词构建：传入 allFacings
//
//  旧：
//      std::wstring promptW = BuildLLMPrompt(allImages);
//
//  新：
//      std::wstring promptW = BuildLLMPrompt(allImages, allFacings);   // ★★
// ---------------------------------------------------------------


// ===================================================================
//  完整替换后的 OnBnClickedButtonAitrain 核心循环（供直接复制）
// ===================================================================
/*

void CThermal_Analysis_CAMDiagnosisDlg::OnBnClickedButtonAitrain()
{
    if (!g_poseSession) { MessageBox(_T("g_poseSession is nullptr!")); return; }
    if (!g_segSession)  { MessageBox(_T("g_segSession is nullptr!"));  return; }

    std::vector<std::map<int, RegionTempStat>> allImages;
    std::vector<BodyFacing> allFacings;   // ★★ 新增
    cv::Mat aiImge;

    for (int i = 0; i < curNum; i++)
    {
        cv::Mat& image       = Sadd2PatientInfo[i].img;
        cv::Mat& image2tempt = Sadd2PatientInfo[i].img;
        if (image.empty()) continue;

        // --- 人体分割 ---
        cv::Mat humanMaskDetected = RunHumanSegmentation(image);
        cv::Mat mask1     = RefineMaskMorph(humanMaskDetected);
        cv::Mat mask2     = RefineMaskGrabCut(image, mask1);
        cv::Mat finalMask = ConservativeErode(mask2);

        cv::Mat foreground = cv::Mat::zeros(image.size(), image.type());
        image.copyTo(foreground, finalMask);
        aiImge = foreground;

        // --- Pose 推理（代码不变，略）---
        LetterBoxInfo lb;
        cv::Mat letterboxImg = LetterBox(foreground, 640, lb);
        // ... 推理、PostProcessYOLOv8Pose ...
        if (poses.empty()) continue;

        // --- 可视化骨骼（代码不变，略）---

        PosePerson& person = poses[0];
        cv::Mat humanMask = finalMask;

        // ★★ 调用新版 BuildBodyRegionMask，同时获取朝向
        BodyFacing facing;
        cv::Mat bodyRegionMask = BuildBodyRegionMask(
            person, image.cols, image.rows, facing);

        // ★★ 在图像左上角标注朝向文字，方便直观确认
        {
            CString facingText;
            facingText.Format(_T("[%s]"), CString(BodyFacingName[facing]));
            cv::putText(
                image,
                std::string(CT2A(facingText)),
                cv::Point(8, 24),
                cv::FONT_HERSHEY_SIMPLEX, 0.7,
                facing == FACING_FRONT  ? cv::Scalar(0, 255, 0)   // 绿 = 正面
              : facing == FACING_BACK   ? cv::Scalar(0, 0, 255)   // 红 = 背面
                                        : cv::Scalar(0, 200, 255),// 橙 = 未知
                2, cv::LINE_AA
            );
        }

        // --- 温度统计 ---
        auto regionStats = CalcRegionTempFromMatrix(
            Sadd2PatientInfo[i].tempbuf,
            bodyRegionMask,
            humanMask
        );

        allImages.push_back(regionStats);
        allFacings.push_back(facing);    // ★★

        // --- 可视化（标注最冷/最热点 + 区域名（含正背面）+ 均值 + 标准差）---
        for (auto& kv : regionStats)
        {
            int region = kv.first;
            if (region <= 0 || region >= REGION_COUNT) continue;
            const RegionTempStat& stat = kv.second;

            cv::circle(image, stat.minPt, 4, cv::Scalar(255, 0, 0), -1);
            cv::circle(image, stat.maxPt, 4, cv::Scalar(0, 0, 255), -1);

            cv::Mat regionMaskSingle = (bodyRegionMask == region);
            cv::Rect bbox = cv::boundingRect(regionMaskSingle);
            if (bbox.area() <= 0) continue;

            // ★★ 使用 GetRegionName 获取朝向感知的名称
            CString text;
            text.Format(
                "%s %.1fC(\xB1%.1f)",
                CString(GetRegionName(region, facing)),
                stat.meanTemp,
                stat.stdDev
            );
            cv::putText(
                image,
                std::string(CT2A(text)),
                cv::Point(bbox.x + 3, bbox.y + 15),
                cv::FONT_HERSHEY_SIMPLEX, 0.45,
                RegionColor[region], 1, cv::LINE_AA
            );
        }

        // --- 绘制轮廓 ---
        for (int r = REGION_HEAD; r < REGION_UNKNOWN; r++)
        {
            cv::Mat regionMaskSingle = (bodyRegionMask == r) & finalMask;
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(regionMaskSingle, contours,
                             cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::drawContours(image, contours, -1, RegionColor[r], 2);
        }
    }

    // ★★ BuildLLMPrompt 传入 allFacings
    std::wstring promptW = BuildLLMPrompt(allImages, allFacings);
    CStringW input(promptW.c_str());

    CString output = CallGemma(aiImge, input);
    AfxMessageBox(output);
}

*/
