#include "MainFrame.h"
#include "ui/Theme.h"
#include "ui/I18n.h"
#include "ui/ImportDlg.h"
#include "ui/ExportDlg.h"
#include "core/Exporter.h"
#include "resource.h"

#include <shellapi.h>

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_LBN_SELCHANGE(IDC_GAME_LIST, OnSelChangeGame)
    ON_COMMAND(ID_VIEW_REFRESH, OnRefresh)
    ON_BN_CLICKED(IDC_BTN_IMPORT, OnImport)
    ON_BN_CLICKED(IDC_BTN_EXPORT, OnExport)
    ON_BN_CLICKED(IDC_BTN_LANG, OnLangToggle)
    ON_MESSAGE(WM_SHOTS_CHANGED, OnShotsChanged)
END_MESSAGE_MAP()

CMainFrame::CMainFrame()
{
    CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                      ::LoadCursor(nullptr, IDC_ARROW),
                                      nullptr, // 背景自绘
                                      AfxGetApp()->LoadIcon(IDR_MAINFRAME));
    Create(cls, I18n::T(S_TITLE), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    // 顶部右侧"导入"按钮
    m_btnImport.Create(I18n::T(S_IMPORT_BTN), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       CRect(0, 0, 0, 0), this, IDC_BTN_IMPORT);
    m_btnImport.SetFont(&Theme::Font());

    // 导出按钮(导入右侧)
    m_btnExport.Create(I18n::T(S_EXPORT_BTN), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       CRect(0, 0, 0, 0), this, IDC_BTN_EXPORT);
    m_btnExport.SetFont(&Theme::Font());

    // 语言切换按钮(导入左侧):显示当前语言名,点击中英互换
    m_btnLang.Create(I18n::LangName(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     CRect(0, 0, 0, 0), this, IDC_BTN_LANG);
    m_btnLang.SetFont(&Theme::Font());

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
    m_lastGames = games;
    m_lastTotal = m_store.TotalCount();

    if (games == 0)
    {
        m_header.SetWindowText(I18n::T(S_NO_STEAM));
        m_gameList.SetGames(nullptr);
        m_grid.SetGame(nullptr);
        return;
    }

    RefreshHeaderText();

    m_gameList.SetGames(&m_store.Games());
    if (!m_store.Games().empty())
    {
        m_gameList.SetCurSel(0);
        m_grid.SetGame(&m_store.Games()[0]);
    }
}

void CMainFrame::RefreshHeaderText()
{
    // 顶栏统计:使用缓存数字,语言切换时无需重扫
    CString title;
    title.Format(I18n::T(S_STATS), m_lastGames, m_lastTotal);
    m_header.SetWindowText(title);
}

void CMainFrame::OnRefresh()
{
    LoadData();
    Invalidate(FALSE);
}

void CMainFrame::OnLangToggle()
{
    I18n::Toggle();                       // 切换并持久化
    SetWindowText(I18n::T(S_TITLE));      // 窗口标题
    m_btnImport.SetWindowText(I18n::T(S_IMPORT_BTN));
    m_btnExport.SetWindowText(I18n::T(S_EXPORT_BTN));
    m_btnLang.SetWindowText(I18n::LangName());
    RefreshHeaderText();                  // 顶栏统计(用缓存,免重扫)

    // 列表/网格为自绘,重绘时自动取新语言的字符串
    m_gameList.Invalidate(FALSE);
    m_grid.Invalidate(FALSE);
}

void CMainFrame::OnExport()
{
    // 需先选中一个游戏
    int sel = m_gameList.GetCurSel();
    if (sel < 0 || static_cast<size_t>(sel) >= m_store.Games().size())
    {
        MessageBox(I18n::T(S_SELECT_FIRST), I18n::T(S_EXPORT_CAP), MB_ICONINFORMATION);
        return;
    }

    // ① 定位 ffmpeg
    CString ffmpeg;
    if (!Exporter::LocateFfmpeg(ffmpeg))
    {
        // 提示下载并加入 PATH;"是"直接打开下载页
        if (MessageBox(I18n::T(S_NO_FFMPEG), I18n::T(S_EXPORT_CAP),
                       MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            ::ShellExecute(nullptr, L"open",
                           L"https://www.gyan.dev/ffmpeg/builds/",
                           nullptr, nullptr, SW_SHOW);
        }
        return;
    }

    // ② 探测 AV1 编码器(选目录之前,避免白选)
    CString encoder = Exporter::ProbeEncoder(ffmpeg);
    if (encoder.IsEmpty())
    {
        MessageBox(I18n::T(S_NO_ENCODER), I18n::T(S_EXPORT_CAP), MB_ICONERROR);
        return;
    }

    // ③ 选择目标根目录
    CString dstRoot;
    if (!Exporter::PickFolder(GetSafeHwnd(), dstRoot))
        return; // 用户取消

    // ④ 导出进度对话框
    const GameShots& game = m_store.Games()[sel];
    CExportDlg dlg(&game, ffmpeg, encoder, dstRoot, this);
    dlg.DoModal(); // 导出只写目标目录,不触碰 Steam 目录,无需刷新
}

void CMainFrame::LayoutChildren()
{
    CRect rc;
    GetClientRect(rc);

    // 右侧三个按钮:语言(70)最右,导出(100)居中,导入(100)在左;标题让出位置
    int btnH = 26, btnPad = 8, gap = 6;
    int langW = 70, expW = 100, impW = 100;

    int xLang   = rc.Width() - btnPad - langW;
    int xExport = xLang - gap - expW;
    int xImport = xExport - gap - impW;

    m_header.MoveWindow(0, 0, xImport - btnPad, kHeaderH);
    m_btnImport.MoveWindow(xImport, (kHeaderH - btnH) / 2, impW, btnH);
    m_btnExport.MoveWindow(xExport, (kHeaderH - btnH) / 2, expW, btnH);
    m_btnLang.MoveWindow(xLang, (kHeaderH - btnH) / 2, langW, btnH);

    m_gameList.MoveWindow(0, kHeaderH, kListWidth, rc.Height() - kHeaderH);
    m_grid.MoveWindow(kListWidth, kHeaderH, rc.Width() - kListWidth, rc.Height() - kHeaderH);
}

LRESULT CMainFrame::OnShotsChanged(WPARAM, LPARAM)
{
    // 记录当前选中游戏的 AppId,刷新后尽量定位回去
    unsigned int curAppId = 0;
    int sel = m_gameList.GetCurSel();
    if (sel >= 0 && static_cast<size_t>(sel) < m_store.Games().size())
        curAppId = m_store.Games()[sel].AppId;

    m_store.Scan();

    // 更新标题统计
    m_lastGames = static_cast<int>(m_store.Games().size());
    m_lastTotal = m_store.TotalCount();
    if (m_lastGames == 0)
    {
        m_header.SetWindowText(I18n::T(S_NO_STEAM));
        m_gameList.SetGames(nullptr);
        m_grid.SetGame(nullptr);
        return 0;
    }
    RefreshHeaderText();

    m_gameList.SetGames(&m_store.Games());

    // 找回原游戏位置
    int target = 0;
    if (curAppId != 0)
    {
        for (size_t i = 0; i < m_store.Games().size(); ++i)
        {
            if (m_store.Games()[i].AppId == curAppId)
            {
                target = static_cast<int>(i);
                break;
            }
        }
    }
    m_gameList.SetCurSel(target);
    m_grid.SetGame(&m_store.Games()[target]);
    return 0;
}

void CMainFrame::OnImport()
{
    int sel = m_gameList.GetCurSel();
    if (sel < 0 || static_cast<size_t>(sel) >= m_store.Games().size())
    {
        MessageBox(I18n::T(S_SELECT_FIRST), I18n::T(S_TIP), MB_ICONINFORMATION);
        return;
    }

    const GameShots& game = m_store.Games()[sel];
    // 显示名:未知名称回退 "App <id>"(游戏名本身不翻译)
    CString displayName;
    if (game.Name.IsEmpty())
        displayName.Format(L"App %u", game.AppId);
    else
        displayName = game.Name;

    CImportDlg dlg(game.Dir, displayName, this);
    if (dlg.DoModal() == IDOK && dlg.Imported())
    {
        OnRefresh(); // 重新扫描,显示新导入的截图
    }
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
