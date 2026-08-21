#include "steamshot-mgr.h"
#include "MainFrame.h"

CSteamShotMgrApp theApp;

BOOL CSteamShotMgrApp::InitInstance()
{
    // 显式初始化 GDI+:导入功能的 JPEG 编码直接使用 Gdiplus::Bitmap::Save
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
    {
        AfxMessageBox(L"GDI+ 初始化失败", MB_ICONERROR);
        return FALSE;
    }

    CMainFrame* pFrame = new CMainFrame();
    if (!pFrame || !pFrame->GetSafeHwnd())
        return FALSE;

    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
}

int CSteamShotMgrApp::ExitInstance()
{
    if (m_gdiplusToken)
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    return CWinApp::ExitInstance();
}
