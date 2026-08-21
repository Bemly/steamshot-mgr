#include "MainFrame.h"
#include "ui/Theme.h"
#include "resource.h"

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_LBN_SELCHANGE(IDC_GAME_LIST, OnSelChangeGame)
    ON_COMMAND(ID_VIEW_REFRESH, OnRefresh)
END_MESSAGE_MAP()

CMainFrame::CMainFrame()
{
    CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                      ::LoadCursor(nullptr, IDC_ARROW),
                                      nullptr, // 背景自绘
                                      AfxGetApp()->LoadIcon(IDR_MAINFRAME));
    Create(cls, L"Steam 截图管理器", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
           CRect(100, 100, 1280, 800));
}

int CMainFrame::OnCreate(LPCREATESTRUCT cs)
{
    if (CFrameWnd::OnCreate(cs) == -1)
        return -1;

    // 顶部标题栏
    m_header.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    CRect(0, 0, 0, 0), this, IDC_HEADER);
    m_header.SetFont(&Theme::FontBold());

    // 左侧游戏列表(自绘)
    m_gameList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_OWNERDRAWFIXED |
                      LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                      CRect(0, 0, 0, 0), this, IDC_GAME_LIST);

    // 右侧缩略图网格
    m_grid.Create(this, IDC_THUMB_GRID);

    LoadData();
    LayoutChildren();
    return 0;
}

void CMainFrame::LoadData()
{
    int games = m_store.Scan();

    if (games == 0)
    {
        m_header.SetWindowText(L"未找到 Steam 截图 — 请确认已安装 Steam 并有截图记录");
        m_gameList.SetGames(nullptr);
        m_grid.SetGame(nullptr);
        return;
    }

    CString title;
    title.Format(L"截图  ·  %d 款游戏  ·  %d 张截图", games, m_store.TotalCount());
    m_header.SetWindowText(title);

    m_gameList.SetGames(&m_store.Games());
    if (!m_store.Games().empty())
    {
        m_gameList.SetCurSel(0);
        m_grid.SetGame(&m_store.Games()[0]);
    }
}

void CMainFrame::OnRefresh()
{
    LoadData();
    Invalidate(FALSE);
}

void CMainFrame::LayoutChildren()
{
    CRect rc;
    GetClientRect(rc);

    m_header.MoveWindow(0, 0, rc.Width(), kHeaderH);
    m_gameList.MoveWindow(0, kHeaderH, kListWidth, rc.Height() - kHeaderH);
    m_grid.MoveWindow(kListWidth, kHeaderH, rc.Width() - kListWidth, rc.Height() - kHeaderH);
}

void CMainFrame::OnSize(UINT type, int cx, int cy)
{
    CFrameWnd::OnSize(type, cx, cy);
    if (::IsWindow(m_grid.GetSafeHwnd()))
        LayoutChildren();
}

BOOL CMainFrame::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(rc);
    pDC->FillRect(rc, &Theme::BkBrush());

    // 顶部栏底色
    CRect rcHeader(0, 0, rc.Width(), kHeaderH);
    pDC->FillRect(rcHeader, &Theme::PanelBrush());

    // 分隔线
    CPen pen(PS_SOLID, 1, RGB(0x10, 0x14, 0x1A));
    CPen* oldPen = pDC->SelectObject(&pen);
    pDC->MoveTo(0, kHeaderH - 1);
    pDC->LineTo(rc.Width(), kHeaderH - 1);
    pDC->MoveTo(kListWidth, kHeaderH);
    pDC->LineTo(kListWidth, rc.Height());
    pDC->SelectObject(oldPen);
    return TRUE;
}

HBRUSH CMainFrame::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    // 顶部静态控件
    if (pWnd && pWnd->GetDlgCtrlID() == IDC_HEADER)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(Theme::Text());
        return Theme::PanelBrush();
    }
    return CFrameWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CMainFrame::OnSelChangeGame()
{
    int sel = m_gameList.GetCurSel();
    if (sel < 0 || static_cast<size_t>(sel) >= m_store.Games().size())
        return;
    m_grid.SetGame(&m_store.Games()[sel]);
}
