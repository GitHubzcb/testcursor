// ===================================================================
//  OnBnClickedButtonAitrain_patch.cpp
//
//  本文件展示对 OnBnClickedButtonAitrain 函数中
//  "数据收集" 和 "可视化" 两处的修改片段。
//  请将以下代码替换到原函数对应位置。
// ===================================================================

// ---------------------------------------------------------------
//  [修改1] allImages 的元素类型由
//      std::map<CStringW, double>
//  改为
//      std::map<int, RegionTempStat>
//  使其能携带完整统计信息（均值、最高、最低、标准差）。
//  原声明替换如下：
// ---------------------------------------------------------------

//  旧代码：
//      std::vector<std::map<CStringW, double>> allImages;
//
//  新代码：
//      std::vector<std::map<int, RegionTempStat>> allImages;


// ---------------------------------------------------------------
//  [修改2] 循环末尾：收集当前图像统计数据
//  原代码将 meanTemp 存入 map<CStringW, double>，
//  新代码直接存储完整 RegionTempStat 对象。
//
//  替换以下旧代码段：
//
//  旧：
//      std::map<CStringW, double> imageData;
//      for (auto& kv : regionStats)
//      {
//          ...imageData[regionName] = stat.meanTemp;
//      }
//      allImages.push_back(imageData);
//
//  新：
// ---------------------------------------------------------------
/*
    // 直接保存完整统计（含均值、最高、最低、标准差）
    allImages.push_back(regionStats);
*/


// ---------------------------------------------------------------
//  [修改3] 可视化：在图像上标注区域信息
//  原代码只显示区域名 + meanTemp，
//  新代码额外显示左右信息（区域名已包含"(左)"/"(右)"后缀）。
//  RegionColor 数组大小同步扩展，此处只需保证 region < REGION_COUNT。
//
//  对应替换段（无需改动，仅需确保 BodyRegionName 已更新）：
//
//      CString text;
//      text.Format(
//          "%s %.1fC",
//          BodyRegionName[region],
//          stat.meanTemp
//      );
// ---------------------------------------------------------------


// ---------------------------------------------------------------
//  [修改4] 提示词构建：替换原来的 input 构建逻辑
//
//  旧（仅输出均值）：
//      for (auto& kv : allImages[i])
//          tmp.Format(L"  %ls：%.1f℃\n", kv.first, kv.second);
//
//  新：调用 BuildLLMPrompt，自动生成左右、差值、标准差格式的提示词：
// ---------------------------------------------------------------
/*
    std::wstring promptW = BuildLLMPrompt(allImages);
    CStringW input(promptW.c_str());
    CString output = CallGemma(aiImge, input);
    AfxMessageBox(output);
*/


// ===================================================================
//  完整替换后的 OnBnClickedButtonAitrain 核心循环（仅关键改动部分）
//  供参考，可直接复制粘贴到原函数中。
// ===================================================================
/*

void CThermal_Analysis_CAMDiagnosisDlg::OnBnClickedButtonAitrain()
{
    if (!g_poseSession) { MessageBox(_T("g_poseSession is nullptr!")); return; }
    if (!g_segSession)  { MessageBox(_T("g_segSession is nullptr!"));  return; }

    // ★ 改为存储完整统计
    std::vector<std::map<int, RegionTempStat>> allImages;
    cv::Mat aiImge;

    for (int i = 0; i < curNum; i++)
    {
        cv::Mat& image        = Sadd2PatientInfo[i].img;
        cv::Mat& image2tempt  = Sadd2PatientInfo[i].img;
        if (image.empty()) continue;

        // --- 人体分割 ---
        cv::Mat humanMaskDetected = RunHumanSegmentation(image);
        cv::Mat mask1     = RefineMaskMorph(humanMaskDetected);
        cv::Mat mask2     = RefineMaskGrabCut(image, mask1);
        cv::Mat finalMask = ConservativeErode(mask2);

        cv::Mat foreground = cv::Mat::zeros(image.size(), image.type());
        image.copyTo(foreground, finalMask);
        aiImge = foreground;

        // --- Pose 推理 ---
        LetterBoxInfo lb;
        cv::Mat letterboxImg = LetterBox(foreground, 640, lb);
        // ... (推理代码不变) ...
        // std::vector<PosePerson> poses = PostProcessYOLOv8Pose(...);
        if (poses.empty()) continue;

        // --- 可视化骨骼 ---
        // ... (骨骼绘制代码不变) ...

        PosePerson& person = poses[0];
        cv::Mat humanMask = finalMask;

        // ★ 使用新版 BuildBodyRegionMask（已支持左右分侧）
        cv::Mat bodyRegionMask = BuildBodyRegionMask(person, image.cols, image.rows);

        // --- 温度统计 ---
        // ★ 使用新版 CalcRegionTempFromMatrix（已支持标准差）
        auto regionStats = CalcRegionTempFromMatrix(
            Sadd2PatientInfo[i].tempbuf,
            bodyRegionMask,
            humanMask
        );

        // ★ 存储完整统计（不再只存 meanTemp）
        allImages.push_back(regionStats);

        // --- 可视化（标注最冷/最热点 + 区域名 + 均值）---
        for (auto& kv : regionStats)
        {
            int region = kv.first;
            if (region <= 0 || region >= REGION_COUNT) continue;
            const RegionTempStat& stat = kv.second;

            // 冷热点
            cv::circle(image, stat.minPt, 4, cv::Scalar(255, 0, 0), -1);
            cv::circle(image, stat.maxPt, 4, cv::Scalar(0, 0, 255), -1);

            // 区域文字（含"(左)"/"(右)"后缀 + 均值 + 标准差）
            cv::Mat regionMaskSingle = (bodyRegionMask == region);
            cv::Rect bbox = cv::boundingRect(regionMaskSingle);
            if (bbox.area() <= 0) continue;

            CString text;
            text.Format(
                "%s %.1fC(±%.1f)",
                CString(BodyRegionName[region]),
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

        // --- 绘制轮廓（遍历所有新区域 ID）---
        for (int r = REGION_HEAD; r < REGION_UNKNOWN; r++)
        {
            cv::Mat regionMaskSingle = (bodyRegionMask == r) & finalMask;
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(regionMaskSingle, contours,
                             cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::drawContours(image, contours, -1, RegionColor[r], 2);
        }
    }

    // ★ 使用新版提示词构建函数
    std::wstring promptW = BuildLLMPrompt(allImages);
    CStringW input(promptW.c_str());

    CString output = CallGemma(aiImge, input);
    AfxMessageBox(output);
}

*/
