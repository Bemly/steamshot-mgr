#include "ThumbGridView.h"
#include "Theme.h"
#include "PreviewDlg.h"
#include "../resource.h"

#include <afxglobals.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <process.h>

#pragma comment(lib, "shlwapi.lib")

BEGIN_MESSAGE_MAP(ThumbGridView, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_CONTEXTMENU()
    ON_WM_ERASEBKGND()
    ON_COMMAND(ID_CTX_OPEN_EXPLORER, OnCtxOpenExplorer)
    ON_COMMAND(ID_CTX_DELETE, OnCtxDelete)
    ON_MESSAGE(WM_THUMB_READY, OnThumbReady)
END_MESSAGE_MAP()

ThumbGridView::ThumbGridView()
{
    ::InitializeCriticalSection(&m_queueLock);
}

ThumbGridView::~ThumbGridView()
{
    StopWorker();
    ::DeleteCriticalSection(&m_queueLock);
}

BOOL ThumbGridView::Create(CWnd* parent, UINT id)
{
    // 注册窗口类(暗色背景,双缓冲需要 CS_OWNDC 不是必须)
    static bool registered = false;
    static LPCTSTR kClass = L"SteamShotThumbGrid";
    if (!registered)
    {
        WNDCLASS wc{};
        wc.style         = CS_DBLCLKS;
        wc.lpfnWndProc   = ::DefWindowProc;
        wc.hInstance     = AfxGetInstanceHandle();
        wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // OnEraseBkgnd 自行处理
        wc.lpszClassName = kClass;
        if (!AfxRegisterClass(&wc))
            return FALSE;
        registered = true;
    }

    return CWnd::Create(kClass, nullptr,
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                        CRect(0, 0, 0, 0), parent, id);
}

int ThumbGridView::OnCreate(LPCREATESTRUCT cs)
{
    if (CWnd::OnCreate(cs) == -1)
        return -1;
    StartWorker();
    return 0;
}

void ThumbGridView::OnDestroy()
{
    StopWorker();
    CWnd::OnDestroy();
}

// ---------------------------------------------------------------------------
// 数据
// ---------------------------------------------------------------------------

void ThumbGridView::SetGame(const GameShots* game)
{
    ClearCacheAndQueue();
    m_game     = game;
    m_selected = -1;
    m_scrollPos = 0;
    RecalcLayout();
    Invalidate(FALSE);
}

void ThumbGridView::ClearCacheAndQueue()
{
    ::EnterCriticalSection(&m_queueLock);
    ++m_requestGen; // 让队列中旧任务作废
    m_pending.clear();
    m_queued.clear();
    ::LeaveCriticalSection(&m_queueLock);
    m_cache.clear();
}

void ThumbGridView::RecalcLayout()
{
    CRect rc;
    GetClientRect(rc);
    int availW = max(rc.Width() - kMargin * 2, kCellW);
    m_cols = max(availW / kCellW, 1);

    int rows = 0;
    if (m_game && !m_game->Shots.empty())
        rows = static_cast<int>((m_game->Shots.size() + m_cols - 1) / m_cols);
    m_totalH = rows * kCellH + kMargin * 2;

    // 滚动条
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = max(m_totalH - 1, 0);
    si.nPage  = rc.Height();
    si.nPos   = m_scrollPos;
    SetScrollInfo(SB_VERT, &si, TRUE);
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

BOOL ThumbGridView::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE; // 全部在 OnPaint 双缓冲绘制
}

void ThumbGridView::OnPaint()
{
    CPaintDC dc(this);
    CRect rcClient;
    GetClientRect(rcClient);

    // 双缓冲
    CMemDC memDC(dc, rcClient);
    CDC& dcMem = memDC.GetDC();
    dcMem.FillRect(rcClient, &Theme::BkBrush());
    dcMem.SetBkMode(TRANSPARENT);

    if (!m_game || m_game->Shots.empty())
    {
        dcMem.SelectObject(&Theme::Font());
        dcMem.SetTextColor(Theme::TextDim());
        dcMem.DrawText(L"在左侧选择一个游戏以浏览截图", rcClient,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    int firstRow = max((m_scrollPos - kMargin) / kCellH, 0);
    int lastRow  = (m_scrollPos + rcClient.Height() + kCellH) / kCellH + 1;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int col = 0; col < m_cols; ++col)
        {
            int index = row * m_cols + col;
            if (index >= static_cast<int>(m_game->Shots.size()))
                break;

            CRect cell;
            cell.left   = kMargin + col * kCellW;
            cell.top    = kMargin + row * kCellH - m_scrollPos;
            cell.right  = cell.left + kCellW - 8;
            cell.bottom = cell.top + kCellH - 8;

            CRect rcThumb(cell.left, cell.top, cell.right, cell.top + kThumbH);
            CRect rcLabel(cell.left, rcThumb.bottom + 3, cell.right, cell.bottom);

            const ScreenshotItem& shot = m_game->Shots[index];

            // 卡片底
            bool sel = (index == m_selected);
            CBrush cardBrush(sel ? Theme::Selection() : Theme::Panel());
            dcMem.FillRect(cell, &cardBrush);
            if (sel)
            {
                CPen pen(PS_SOLID, 2, Theme::Accent());
                CPen* oldPen = dcMem.SelectObject(&pen);
                HGDIOBJ oldBrush = dcMem.SelectObject(::GetStockObject(NULL_BRUSH));
                dcMem.Rectangle(cell);
                dcMem.SelectObject(oldPen);
                dcMem.SelectObject(oldBrush);
            }

            // 缩略图
            const CImage* img = GetThumb(index);
            if (img && !img->IsNull())
            {
                // 等比缩放居中(letterbox)
                double scaleX = static_cast<double>(rcThumb.Width())  / img->GetWidth();
                double scaleY = static_cast<double>(rcThumb.Height()) / img->GetHeight();
                double scale  = min(scaleX, scaleY);
                int w = static_cast<int>(img->GetWidth()  * scale);
                int h = static_cast<int>(img->GetHeight() * scale);
                int x = rcThumb.left + (rcThumb.Width()  - w) / 2;
                int y = rcThumb.top  + (rcThumb.Height() - h) / 2;
                img->Draw(dcMem.m_hDC, x, y, w, h);
            }
            else
            {
                // 占位:暗色块 + 加载提示,并提交解码请求
                CBrush ph(RGB(0x10, 0x14, 0x1A));
                dcMem.FillRect(rcThumb, &ph);
                dcMem.SelectObject(&Theme::FontSmall());
                dcMem.SetTextColor(Theme::TextDim());
                dcMem.DrawText(L"加载中…", rcThumb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                RequestThumb(index);
            }

            // 云端状态徽标:已上传 → 缩略图右下角云朵符号(☁ U+2601)
            if (shot.Cloud == CloudState::Uploaded)
            {
                constexpr int kBadge = 22; // 徽标方块边长
                CRect rcBadge(rcThumb.right - kBadge - 4, rcThumb.bottom - kBadge - 4,
                              rcThumb.right - 4, rcThumb.bottom - 4);
                // 深色底衬,保证在亮图上可读
                CBrush badgeBg(RGB(0x10, 0x14, 0x1A));
                dcMem.FillRect(rcBadge, &badgeBg);
                dcMem.SelectObject(&Theme::FontBadge());
                dcMem.SetTextColor(Theme::Accent());
                dcMem.DrawText(L"\u2601", rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else if (shot.Cloud == CloudState::NotUploaded)
            {
                // 未上传:同位置橙色上箭头(↑ U+2191)
                constexpr int kBadge = 22;
                CRect rcBadge(rcThumb.right - kBadge - 4, rcThumb.bottom - kBadge - 4,
                              rcThumb.right - 4, rcThumb.bottom - 4);
                CBrush badgeBg(RGB(0x10, 0x14, 0x1A));
                dcMem.FillRect(rcBadge, &badgeBg);
                dcMem.SelectObject(&Theme::FontBadge());
                dcMem.SetTextColor(Theme::Orange());
                dcMem.DrawText(L"\u2191", rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else if (shot.Cloud == CloudState::Orphan)
            {
                // 未登记(孤儿):灰色问号,提示启动 Steam 后自动索引(docs §5)
                constexpr int kBadge = 22;
                CRect rcBadge(rcThumb.right - kBadge - 4, rcThumb.bottom - kBadge - 4,
                              rcThumb.right - 4, rcThumb.bottom - 4);
                CBrush badgeBg(RGB(0x10, 0x14, 0x1A));
                dcMem.FillRect(rcBadge, &badgeBg);
                dcMem.SelectObject(&Theme::FontBadge());
                dcMem.SetTextColor(Theme::Gray());
                dcMem.DrawText(L"?", rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // 时间标签(由文件名时间戳生成)
            CString label = shot.Timestamp.Format(L"%Y-%m-%d %H:%M:%S");
            dcMem.SelectObject(&Theme::FontSmall());
            dcMem.SetTextColor(Theme::TextDim());
            dcMem.DrawText(label, rcLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
}

// ---------------------------------------------------------------------------
// 滚动与命中
// ---------------------------------------------------------------------------

void ThumbGridView::OnSize(UINT type, int cx, int cy)
{
    CWnd::OnSize(type, cx, cy);
    RecalcLayout();
}

void ThumbGridView::OnVScroll(UINT code, UINT /*pos*/, CScrollBar* /*bar*/)
{
    int oldPos = m_scrollPos;
    CRect rc;
    GetClientRect(rc);
    int maxPos = max(m_totalH - rc.Height(), 0);

    switch (code)
    {
    case SB_LINEUP:        m_scrollPos -= 40;              break;
    case SB_LINEDOWN:      m_scrollPos += 40;              break;
    case SB_PAGEUP:        m_scrollPos -= rc.Height();     break;
    case SB_PAGEDOWN:      m_scrollPos += rc.Height();     break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_TRACKPOS;
        GetScrollInfo(SB_VERT, &si);
        m_scrollPos = si.nTrackPos;
        break;
    }
    case SB_TOP:           m_scrollPos = 0;                break;
    case SB_BOTTOM:        m_scrollPos = m_totalH;         break;
    default: return;
    }

    m_scrollPos = max(0, min(m_scrollPos, maxPos));
    if (m_scrollPos != oldPos)
    {
        SetScrollPos(SB_VERT, m_scrollPos);
        Invalidate(FALSE);
    }
}

BOOL ThumbGridView::OnMouseWheel(UINT flags, short delta, CPoint pt)
{
    m_scrollPos -= (delta / WHEEL_DELTA) * 80;
    CRect rc;
    GetClientRect(rc);
    int maxPos = max(m_totalH - rc.Height(), 0);
    m_scrollPos = max(0, min(m_scrollPos, maxPos));
    SetScrollPos(SB_VERT, m_scrollPos);
    Invalidate(FALSE);
    return TRUE;
}

int ThumbGridView::HitTest(CPoint pt) const
{
    if (!m_game)
        return -1;
    int col = (pt.x - kMargin) / kCellW;
    int row = (pt.y + m_scrollPos - kMargin) / kCellH;
    if (col < 0 || col >= m_cols || row < 0)
        return -1;
    int index = row * m_cols + col;
    if (index >= static_cast<int>(m_game->Shots.size()))
        return -1;

    // 精确到单元格内部
    CRect cell(kMargin + col * kCellW, kMargin + row * kCellH - m_scrollPos,
               kMargin + (col + 1) * kCellW - 8, kMargin + (row + 1) * kCellH - m_scrollPos - m_scrollPos * 0);
    if (!cell.PtInRect(pt))
        return -1;
    return index;
}

void ThumbGridView::OnLButtonDown(UINT flags, CPoint pt)
{
    SetFocus();
    int hit = HitTest(pt);
    if (hit != m_selected)
    {
        m_selected = hit;
        Invalidate(FALSE);
    }
    CWnd::OnLButtonDown(flags, pt);
}

void ThumbGridView::OnLButtonDblClk(UINT flags, CPoint pt)
{
    int hit = HitTest(pt);
    if (hit >= 0)
        OpenPreview(hit);
    CWnd::OnLButtonDblClk(flags, pt);
}

void ThumbGridView::OpenPreview(int index)
{
    if (!m_game || index < 0 || index >= static_cast<int>(m_game->Shots.size()))
        return;

    CPreviewDlg dlg(m_game->Shots, index, this);
    dlg.DoModal();
    m_selected = static_cast<int>(dlg.CurrentIndex());
    Invalidate(FALSE);
}

// ---------------------------------------------------------------------------
// 右键菜单: 资源管理器打开 / 删除图片及缩略图
// ---------------------------------------------------------------------------

void ThumbGridView::OnContextMenu(CWnd* /*pWnd*/, CPoint pt)
{
    if (!m_game || m_game->Shots.empty())
        return;

    // pt 为屏幕坐标;右键空白区(pt = -1,-1,来自键盘)则不弹
    if (pt.x == -1 && pt.y == -1)
        return;

    // 屏幕坐标 → 客户区,做命中测试
    CPoint clientPt = pt;
    ScreenToClient(&clientPt);
    int hit = HitTest(clientPt);
    if (hit < 0)
        return; // 点在空白处

    m_ctxIndex = hit;
    m_selected = hit;
    Invalidate(FALSE);

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_CTX_OPEN_EXPLORER, L"在资源管理器中打开");
    menu.AppendMenu(MF_STRING, ID_CTX_DELETE,        L"删除此截图及缩略图");
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, this);
}

void ThumbGridView::OnCtxOpenExplorer()
{
    if (!m_game || m_ctxIndex < 0 ||
        m_ctxIndex >= static_cast<int>(m_game->Shots.size()))
        return;

    const CString& path = m_game->Shots[m_ctxIndex].FilePath;
    // explorer /select,<path> 选中该文件
    CString param = L"/select,\"" + path + L"\"";
    ::ShellExecute(nullptr, L"open", L"explorer.exe", param, nullptr, SW_SHOW);
}

void ThumbGridView::OnCtxDelete()
{
    if (!m_game || m_ctxIndex < 0 ||
        m_ctxIndex >= static_cast<int>(m_game->Shots.size()))
        return;

    DeleteShotAt(m_ctxIndex);
}

void ThumbGridView::DeleteShotAt(int index)
{
    const ScreenshotItem& shot = m_game->Shots[index];

    // 二次确认
    CString msg;
    msg.Format(L"确定删除这张截图吗?\n\n%s\n\n将同时删除其缩略图,且不可恢复。",
               static_cast<LPCTSTR>(shot.FileName));
    if (MessageBox(msg, L"删除截图", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    // 删除原图
    if (::PathFileExists(shot.FilePath))
        ::DeleteFile(shot.FilePath);

    // 删除对应缩略图(优先 ThumbPath,否则按命名规则推导)
    CString thumbPath = shot.ThumbPath;
    if (thumbPath.IsEmpty())
    {
        int pos = shot.FilePath.ReverseFind(L'\\');
        if (pos > 0)
            thumbPath = shot.FilePath.Left(pos) + L"\\thumbnails\\" + shot.FileName;
    }
    if (!thumbPath.IsEmpty() && ::PathFileExists(thumbPath))
        ::DeleteFile(thumbPath);

    // 通知主窗口重新扫描(数据已变,索引失效)
    CWnd* parent = GetParent();
    if (parent)
        parent->PostMessage(WM_SHOTS_CHANGED);
}

// ---------------------------------------------------------------------------
// 缩略图缓存
// ---------------------------------------------------------------------------

const CImage* ThumbGridView::GetThumb(int index)
{
    auto it = m_cache.find(index);
    if (it == m_cache.end())
        return nullptr;
    it->second.LastUse = ++m_clock;
    return it->second.Image.get();
}

void ThumbGridView::EvictCache()
{
    if (m_cache.size() <= kMaxCache)
        return;

    // 找出最久未使用的条目淘汰
    while (m_cache.size() > kMaxCache)
    {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        {
            if (it->second.LastUse < oldest->second.LastUse)
                oldest = it;
        }
        m_cache.erase(oldest);
    }
}

void ThumbGridView::RequestThumb(int index)
{
    ::EnterCriticalSection(&m_queueLock);
    if (m_queued.find(index) == m_queued.end())
    {
        m_pending.push_back(index);
        m_queued.insert(index);
    }
    ::LeaveCriticalSection(&m_queueLock);
    ::SetEvent(m_wakeEvt);
}

LRESULT ThumbGridView::OnThumbReady(WPARAM wParam, LPARAM lParam)
{
    // lParam: 解码完成的索引;wParam: 代次
    if (wParam != m_requestGen)
        return 0; // 已切换游戏,丢弃

    int index = static_cast<int>(lParam);
    ::EnterCriticalSection(&m_queueLock);
    m_queued.erase(index);
    ::LeaveCriticalSection(&m_queueLock);
    Invalidate(FALSE); // 简单整屏重绘(可视项少,代价可接受)
    return 0;
}

// ---------------------------------------------------------------------------
// 后台解码线程
// ---------------------------------------------------------------------------

void ThumbGridView::StartWorker()
{
    m_wakeEvt = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_stopEvt = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_thread  = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, WorkerProc, this, 0, nullptr));
}

void ThumbGridView::StopWorker()
{
    if (m_thread)
    {
        ::SetEvent(m_stopEvt);
        ::WaitForSingleObject(m_thread, 2000);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_wakeEvt) { ::CloseHandle(m_wakeEvt); m_wakeEvt = nullptr; }
    if (m_stopEvt) { ::CloseHandle(m_stopEvt); m_stopEvt = nullptr; }
}

unsigned int __stdcall ThumbGridView::WorkerProc(void* arg)
{
    static_cast<ThumbGridView*>(arg)->WorkerLoop();
    return 0;
}

void ThumbGridView::WorkerLoop()
{
    HANDLE waitHandles[2] = { m_stopEvt, m_wakeEvt };

    for (;;)
    {
        DWORD wait = ::WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) // 停止
            break;

        for (;;)
        {
            // 取出一个任务(从尾部取,后请求的优先 —— 通常是当前可视区)
            int index = -1;
            unsigned int gen = 0;
            ::EnterCriticalSection(&m_queueLock);
            if (!m_pending.empty())
            {
                index = m_pending.back();
                m_pending.pop_back();
                gen   = m_requestGen;
            }
            ::LeaveCriticalSection(&m_queueLock);

            if (index < 0)
                break; // 队列空,回到等待

            // 代次校验:切换过游戏则跳过
            if (gen != m_requestGen)
            {
                ::EnterCriticalSection(&m_queueLock);
                m_queued.erase(index);
                ::LeaveCriticalSection(&m_queueLock);
                continue;
            }

            const GameShots* game = m_game;
            if (!game || index >= static_cast<int>(game->Shots.size()))
                continue;
            const ScreenshotItem& shot = game->Shots[index];

            // 加载:优先 Steam 自带缩略图,否则原图
            CString loadPath = !shot.ThumbPath.IsEmpty() ? shot.ThumbPath : shot.FilePath;

            auto img = std::make_unique<CImage>();
            if (FAILED(img->Load(loadPath)) || img->IsNull())
            {
                // 缩略图损坏时回退原图
                if (loadPath != shot.FilePath)
                {
                    img->Destroy();
                    if (FAILED(img->Load(shot.FilePath)) || img->IsNull())
                        continue;
                }
                else
                {
                    continue;
                }
            }

            // 再次校验代次(解码期间用户可能切了游戏)
            if (gen != m_requestGen)
                continue;

            // 写入缓存
            ::EnterCriticalSection(&m_queueLock);
            // 解码前已经从 pending 弹出,这里直接放入缓存
            CacheEntry entry;
            entry.Image   = std::move(img);
            entry.LastUse = ++m_clock;
            m_cache[index] = std::move(entry);
            ::LeaveCriticalSection(&m_queueLock);

            EvictCache();
            PostMessage(WM_THUMB_READY, gen, index);
        }
    }
}
