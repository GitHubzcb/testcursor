#include "ExcelFileUtils.h"

CString GetFileNameWithoutExt(const CString& filePath)
{
	CString fileName;

	// 提取文件名（带扩展名）：从最后一个反斜杠之后截取
	int pos = filePath.ReverseFind(_T('\\'));
	if (pos != -1)
		fileName = filePath.Mid(pos + 1);
	else
		fileName = filePath;

	// 去掉扩展名：从最后一个点之前截取
	int dotPos = fileName.ReverseFind(_T('.'));
	if (dotPos != -1)
		fileName = fileName.Left(dotPos);

	return fileName;
}
