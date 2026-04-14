#include "PayBoardTestDlg.h"

// 需要包含 Excel OLE Automation 相关的头文件
// #include "CApplication.h"
// #include "CWorkbooks.h"
// #include "CWorkbook.h"

BOOL CPayBoardTestDlg::IsExcelFileOpen(const CString& filePath)
{
	HRESULT hr;
	IUnknown* pUnk = NULL;

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

	for (long i = 1; i <= count; i++)
	{
		CWorkbook book = books.get_Item(COleVariant(i));
		CString fullName = book.get_FullName();

		if (fullName.CompareNoCase(filePath) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}
