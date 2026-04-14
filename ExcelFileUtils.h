#pragma once

#include <afxwin.h>

// 从完整文件路径中提取不含扩展名的文件名
// 例如: "C:\\Users\\test\\demo.xlsx" -> "demo"
CString GetFileNameWithoutExt(const CString& filePath);
