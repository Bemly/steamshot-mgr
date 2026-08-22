#include "steamshot-mgr.h"
#include "MainFrame.h"
#include "ui/I18n.h"

CSteamShotMgrApp theApp;

BOOL CSteamShotMgrApp::InitInstance()
{
    // 语言:读用户偏好,无记录则按系统语言(中文系统→中文,其余→英文)
    I18n::Init();

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
