#pragma once

#include <afxwin.h>
#include <afxdisp.h>

// 前置声明（实际项目中由 Excel 类型库生成）
class CApplication;
class CWorkbooks;
class CWorkbook;

class CPayBoardTestDlg : public CDialogEx
{
public:
	// 判断指定绝对路径的 Excel 文件是否已在 Excel 中打开
	// filePath: Excel 文件的绝对路径，例如 "C:\\Data\\report.xlsx"
	// 返回 TRUE 表示文件已打开，FALSE 表示未打开或无法检测
	BOOL IsExcelFileOpen(const CString& filePath);
};
