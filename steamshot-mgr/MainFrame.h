#pragma once

#include <afxwin.h>
#include "core/ScreenshotStore.h"
#include "ui/GameListCtrl.h"
#include "ui/ThumbGridView.h"

// ---------------------------------------------------------------------------
// CMainFrame —— 主窗口:左侧游戏列表 + 右侧缩略图网格(Steam 截图管理器布局)
// ---------------------------------------------------------------------------
class CMainFrame : public CFrameWnd
{
public:
    CMainFrame();

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSelChangeGame();
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnRefresh();
    afx_msg void OnImport();
    DECLARE_MESSAGE_MAP()

private:
    ScreenshotStore m_store;
    GameListCtrl    m_gameList;
    ThumbGridView   m_grid;
    CStatic         m_header;     // 顶部标题栏
    CButton         m_btnImport;  // 顶部右侧"导入"按钮

    static constexpr int kListWidth  = 260; // 左栏宽
    static constexpr int kHeaderH    = 40;  // 顶部栏高

    void LayoutChildren();
    void LoadData();
};
