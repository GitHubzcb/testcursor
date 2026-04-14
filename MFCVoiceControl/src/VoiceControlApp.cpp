#include "stdafx.h"
#include "VoiceControlApp.h"
#include "VoiceControlDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CVoiceControlApp, CWinApp)
END_MESSAGE_MAP()

CVoiceControlApp theApp;

CVoiceControlApp::CVoiceControlApp()
{
}

BOOL CVoiceControlApp::InitInstance()
{
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    CWinApp::InitInstance();

    CVoiceControlDlg dlg;
    m_pMainWnd = &dlg;

    INT_PTR nResponse = dlg.DoModal();

    return FALSE;
}
