#include "PayBoardTestDlg.h"

// 需要包含 Excel OLE Automation 相关的头文件
// #include "CApplication.h"
// #include "CWorkbooks.h"
// #include "CWorkbook.h"

// 通过原始 IDispatch 调用获取属性值（字符串类型）
// 绕过 MFC 包装类中可能错误的 DISPID 映射
static CString GetDispatchPropertyString(LPDISPATCH pDisp, LPOLESTR propName)
{
	CString result;
	if (pDisp == nullptr)
		return result;

	DISPID dispid;
	HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &propName, 1,
		LOCALE_USER_DEFAULT, &dispid);
	if (FAILED(hr))
		return result;

	DISPPARAMS dispparamsNoArgs = { NULL, NULL, 0, 0 };
	VARIANT varResult;
	VariantInit(&varResult);

	hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
		DISPATCH_PROPERTYGET, &dispparamsNoArgs, &varResult, NULL, NULL);

	if (SUCCEEDED(hr) && varResult.vt == VT_BSTR)
	{
		result = varResult.bstrVal;
	}

	VariantClear(&varResult);
	return result;
}

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

		// 绕过 CWorkbook::get_FullName()，直接通过 IDispatch 按属性名调用
		// 避免 MFC 包装类 DISPID 映射错误导致返回 Name 而非 FullName
		CString fullName = GetDispatchPropertyString(
			book.m_lpDispatch, L"FullName");

		if (fullName.CompareNoCase(filePath) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}
