#pragma once

#include <afxwin.h>
#include "resource.h"

// ---------------------------------------------------------------------------
// CSteamShotMgrApp —— 应用程序类
// ---------------------------------------------------------------------------
class CSteamShotMgrApp : public CWinApp
{
public:
    BOOL InitInstance() override;
};

extern CSteamShotMgrApp theApp;
