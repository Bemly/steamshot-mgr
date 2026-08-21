#pragma once

#include <afxwin.h>
#include <gdiplus.h>
#include "resource.h"

// ---------------------------------------------------------------------------
// CSteamShotMgrApp —— 应用程序类
// ---------------------------------------------------------------------------
class CSteamShotMgrApp : public CWinApp
{
public:
    BOOL InitInstance() override;
    int  ExitInstance() override;

private:
    ULONG_PTR m_gdiplusToken = 0; // GDI+ 句柄
};

extern CSteamShotMgrApp theApp;
