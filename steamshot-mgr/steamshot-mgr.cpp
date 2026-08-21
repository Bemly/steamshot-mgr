#include "steamshot-mgr.h"
#include "MainFrame.h"

CSteamShotMgrApp theApp;

BOOL CSteamShotMgrApp::InitInstance()
{
    // CImage(GDI+)在首次使用时自动初始化,MFC 应用程序无需显式 GdiplusStartup
    CMainFrame* pFrame = new CMainFrame();
    if (!pFrame || !pFrame->GetSafeHwnd())
        return FALSE;

    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
}
