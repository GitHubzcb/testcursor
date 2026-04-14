#include "PayBoardTestDlg.h"
#include "ExcelFileUtils.h"

// 需要包含 Excel OLE Automation 相关的头文件
// #include "CApplication.h"
// #include "CWorkbooks.h"
// #include "CWorkbook.h"

BOOL CPayBoardTestDlg::IsExcelFileOpen(const CString& filePath)
{
	HRESULT hr;
	IUnknown* pUnk = NULL;

	// 尝试获取正在运行的 Excel 实例
	hr = ::GetActiveObject(CLSID_ExcelApplication, NULL, &pUnk);
	if (FAILED(hr) || pUnk == NULL)
		return FALSE;

	LPDISPATCH pDisp = nullptr;
	hr = pUnk->QueryInterface(IID_IDispatch, (void**)&pDisp);
	pUnk->Release();

	if (FAILED(hr) || pDisp == nullptr)
		return FALSE;

	CApplication excelApp;
	excelApp.AttachDispatch(pDisp);

	CWorkbooks books = excelApp.get_Workbooks();
	long count = books.get_Count();
	CString targetName = GetFileNameWithoutExt(filePath);

	for (long i = 1; i <= count; i++)
	{
		CWorkbook book = books.get_Item(COleVariant(i));
		CString name = book.get_Name();

		// 去掉工作簿名称的扩展名后与目标文件名做不区分大小写的比较
		int dotPos = name.ReverseFind(_T('.'));
		if (dotPos != -1)
			name = name.Left(dotPos);

		if (name.CompareNoCase(targetName) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}
