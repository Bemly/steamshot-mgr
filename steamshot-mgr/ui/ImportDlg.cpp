#include "ImportDlg.h"
#include "Theme.h"
#include "I18n.h"
#include "../resource.h"

#include <process.h>

BEGIN_MESSAGE_MAP(CImportDlg, CDialog)
    ON_WM_DESTROY()
    ON_WM_DROPFILES()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_CTLCOLOR()
    ON_WM_MEASUREITEM()
    ON_WM_DRAWITEM()
    ON_BN_CLICKED(IDC_BTN_BROWSE, OnBnClickedBrowse)
    ON_BN_CLICKED(IDC_BTN_DONE, OnBnClickedDone)
    ON_BN_CLICKED(IDCANCEL, OnBnClickedCancel)
    ON_LBN_DBLCLK(IDC_IMP_LIST, OnListDblClk)
    ON_MESSAGE(WM_IMPORT_DONE, OnImportDone)
END_MESSAGE_MAP()

CImportDlg::CImportDlg(LPCTSTR gameDir, LPCTSTR gameName, CWnd* parent)
    : CDialog(IDD_IMPORT, parent), m_gameDir(gameDir), m_gameName(gameName)
{
    ::InitializeCriticalSection(&m_lock);
}

void CImportDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

// ---------------------------------------------------------------------------
// 初始化 / 布局
// ---------------------------------------------------------------------------

BOOL CImportDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CString cap;
    cap.Format(I18n::T(S_IMP_TITLE), static_cast<LPCTSTR>(m_gameName));
    SetWindowText(cap);

    // 窗口 80% 居中
    int w = ::GetSystemMetrics(SM_CXSCREEN) * 3 / 5;
    int h = ::GetSystemMetrics(SM_CYSCREEN) * 3 / 5;
    MoveWindow((::GetSystemMetrics(SM_CXSCREEN) - w) / 2,
               (::GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h);

    // tmp 目录: <gameDir>\..\..\tmp  →  760\remote\tmp
    // gameDir = ...\760\remote\<AppID>\screenshots
    CString remote = m_gameDir;
    int p = remote.ReverseFind(L'\\'); remote = remote.Left(p); // <AppID>
    p = remote.ReverseFind(L'\\');      remote = remote.Left(p); // remote
    m_tmpDir = remote + L"\\tmp";
    ::CreateDirectory(m_tmpDir, nullptr);

    m_nameGen = std::make_unique<ShotNameGen>(m_gameDir);

    // 顶部提示
    m_hint.Create(I18n::T(S_IMP_HINT),
                  WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 0, 0), this, IDC_IMP_HINT);
    m_hint.SetFont(&Theme::Font());

    // 列表(自绘)
    m_list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                  LBS_OWNERDRAWVARIABLE | LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                  CRect(0, 0, 0, 0), this, IDC_IMP_LIST);

    // 按钮
    m_btnBrowse.Create(I18n::T(S_PICK_FILES), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       CRect(0, 0, 0, 0), this, IDC_BTN_BROWSE);
    m_btnDone.Create(I18n::T(S_DONE), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     CRect(0, 0, 0, 0), this, IDC_BTN_DONE);
    m_btnCancel.Create(I18n::T(S_CANCEL), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                       CRect(0, 0, 0, 0), this, IDCANCEL);
    m_btnBrowse.SetFont(&Theme::Font());
    m_btnDone.SetFont(&Theme::Font());
    m_btnCancel.SetFont(&Theme::Font());

    // 接受拖入
    DragAcceptFiles(TRUE);

    LayoutControls();
    StartWorker();
    return TRUE;
}

void CImportDlg::LayoutControls()
{
    CRect rc;
    GetClientRect(rc);
    int pad = 12;
    int btnW = 110, btnH = 32, gap = 10;
    int hintH = 26;

    m_hint.MoveWindow(pad, pad, rc.Width() - pad * 2, hintH);

    int bottom = rc.Height() - pad - btnH;
    m_list.MoveWindow(pad, pad + hintH + 6, rc.Width() - pad * 2, bottom - (pad + hintH + 6));

    int bx = rc.Width() - pad - btnW;
    m_btnCancel.MoveWindow(bx, bottom, btnW, btnH);
    bx -= btnW + gap;
    m_btnDone.MoveWindow(bx, bottom, btnW, btnH);
    bx = pad;
    m_btnBrowse.MoveWindow(bx, bottom, btnW + 20, btnH);
}

void CImportDlg::OnSize(UINT type, int cx, int cy)
{
    CDialog::OnSize(type, cx, cy);
    if (::IsWindow(m_list.GetSafeHwnd()))
        LayoutControls();
}

void CImportDlg::OnDestroy()
{
    CancelDateEdit();
    StopWorker();
    DragAcceptFiles(FALSE);
    CleanupTmp();
    CDialog::OnDestroy();
}

// ---------------------------------------------------------------------------
// 文件加入(拖入 / 选取)
// ---------------------------------------------------------------------------

void CImportDlg::OnDropFiles(HDROP hDrop)
{
    UINT count = ::DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<CString> paths;
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t buf[MAX_PATH]{};
        if (::DragQueryFile(hDrop, i, buf, MAX_PATH))
            paths.emplace_back(buf);
    }
    ::DragFinish(hDrop);
    AddFiles(paths);
}

void CImportDlg::OnBnClickedBrowse()
{
    CString filter = I18n::T(S_IMG_FILTER);
    CFileDialog dlg(TRUE, nullptr, nullptr,
                    OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER, filter, this);

    // 多选缓冲
    std::vector<wchar_t> buf(64 * 1024, 0);
    dlg.m_ofn.lpstrFile = buf.data();
    dlg.m_ofn.nMaxFile = static_cast<DWORD>(buf.size());

    if (dlg.DoModal() != IDOK)
        return;

    std::vector<CString> paths;
    const wchar_t* p = dlg.m_ofn.lpstrFile;
    CString first = p;
    p += first.GetLength() + 1;
    if (*p == L'\0')
    {
        paths.push_back(first); // 单选
    }
    else
    {
        while (*p)
        {
            paths.push_back(first + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }
    AddFiles(paths);
}

void CImportDlg::AddFiles(const std::vector<CString>& paths)
{
    static const wchar_t* kExts[] = { L".jpg", L".jpeg", L".png", L".bmp",
                                      L".gif", L".tif", L".tiff", L".webp" };

    int added = 0;
    for (const CString& path : paths)
    {
        CString ext = ::PathFindExtension(path);
        ext.MakeLower();
        bool ok = false;
        for (auto* e : kExts)
        {
            if (ext == e) { ok = true; break; }
        }
        if (!ok)
            continue; // 不支持的扩展名直接跳过

        auto item = std::make_unique<Item>();
        item->SrcPath = path;

        // 取修改时间(CFileStatus.m_mtime 为 CTime,转 COleDateTime)
        CFileStatus st{};
        if (CFile::GetStatus(path, st))
            item->Time = COleDateTime(st.m_mtime.GetTime());
        else
            item->Time = COleDateTime::GetCurrentTime();

        item->FileName = m_nameGen->Generate(item->Time);

        m_items.push_back(std::move(item));
        m_list.AddString(L""); // 占位

        ::EnterCriticalSection(&m_lock);
        m_pending.push_back(static_cast<int>(m_items.size()) - 1);
        ::LeaveCriticalSection(&m_lock);
        ++added;
    }

    if (added > 0)
    {
        ::SetEvent(m_wakeEvt);
        m_list.Invalidate(FALSE);
    }
}

// ---------------------------------------------------------------------------
// 后台批处理线程
// ---------------------------------------------------------------------------

void CImportDlg::StartWorker()
{
    m_wakeEvt = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_stopEvt = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_thread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, WorkerProc, this, 0, nullptr));
}

void CImportDlg::StopWorker()
{
    if (m_thread)
    {
        ::SetEvent(m_stopEvt);
        ::WaitForSingleObject(m_thread, 3000);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_wakeEvt) { ::CloseHandle(m_wakeEvt); m_wakeEvt = nullptr; }
    if (m_stopEvt) { ::CloseHandle(m_stopEvt); m_stopEvt = nullptr; }
}

unsigned int __stdcall CImportDlg::WorkerProc(void* arg)
{
    static_cast<CImportDlg*>(arg)->WorkerLoop();
    return 0;
}

void CImportDlg::WorkerLoop()
{
    HANDLE waitHandles[2] = { m_stopEvt, m_wakeEvt };
    ImageImporter importer;

    for (;;)
    {
        DWORD w = ::WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0)
            break;

        for (;;)
        {
            int index = -1;
            ::EnterCriticalSection(&m_lock);
            if (!m_pending.empty())
            {
                index = m_pending.front();
                m_pending.erase(m_pending.begin());
            }
            ::LeaveCriticalSection(&m_lock);
            if (index < 0)
                break;
            if (index >= static_cast<int>(m_items.size()))
                continue;

            Item& item = *m_items[index];
            importer.Analyze(item.SrcPath, m_tmpDir, item.Result);

            // 生成列表小缩略图(从 tmp jpg 缩,避免解码大图)
            if (!item.Result.TmpPath.IsEmpty())
            {
                CImage full;
                if (SUCCEEDED(full.Load(item.Result.TmpPath)) && !full.IsNull())
                {
                    int tw = 96, th = 44;
                    auto thumb = std::make_unique<CImage>();
                    if (thumb->Create(tw, th, 32))
                    {
                        HDC dc = thumb->GetDC();
                        ::SetStretchBltMode(dc, HALFTONE);
                        // 等比填充(居中裁切可省略,直接拉伸适配小格)
                        full.StretchBlt(dc, 0, 0, tw, th, 0, 0,
                                        full.GetWidth(), full.GetHeight(), SRCCOPY);
                        thumb->ReleaseDC();
                        item.Thumb = std::move(thumb);
                    }
                }
            }
            item.Done = true;

            PostMessage(WM_IMPORT_DONE, 0, index);
        }
    }
}

LRESULT CImportDlg::OnImportDone(WPARAM, LPARAM lParam)
{
    int index = static_cast<int>(lParam);
    if (index >= 0 && index < m_list.GetCount())
    {
        CRect rc;
        m_list.GetItemRect(index, rc);
        m_list.InvalidateRect(rc, FALSE);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 列表自绘
// ---------------------------------------------------------------------------

void CImportDlg::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMis)
{
    if (nIDCtl == IDC_IMP_LIST)
        lpMis->itemHeight = kRowH;
}

void CImportDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis)
{
    if (nIDCtl == IDC_IMP_LIST)
        DrawListItem(lpDis);
}

// 行内布局: [缩略图 96x44] [文件名] ...... [大小] [警告图标]
CRect CImportDlg::FileNameRect(int index) const
{
    CRect rc;
    m_list.GetItemRect(index, rc);
    rc.left += 110;                 // 跳过缩略图
    rc.right = rc.left + 260;       // 文件名区宽
    rc.top += 6;
    rc.bottom = rc.top + 22;
    return rc;
}

void CImportDlg::DrawListItem(LPDRAWITEMSTRUCT lpDis)
{
    int index = lpDis->itemID;
    if (index < 0 || index >= static_cast<int>(m_items.size()))
        return;

    CDC dc;
    dc.Attach(lpDis->hDC);
    CRect rc = lpDis->rcItem;
    Item& item = *m_items[index];

    bool sel = (lpDis->itemState & ODS_SELECTED) != 0;

    // 背景
    CBrush bk(sel ? Theme::Selection() : Theme::Panel());
    dc.FillRect(rc, &bk);

    dc.SetBkMode(TRANSPARENT);

    // 缩略图
    CRect rcThumb(rc.left + 6, rc.top + 6, rc.left + 102, rc.top + 50);
    if (item.Thumb && !item.Thumb->IsNull())
    {
        item.Thumb->Draw(dc.m_hDC, rcThumb);
    }
    else
    {
        CBrush ph(RGB(0x10, 0x14, 0x1A));
        dc.FillRect(rcThumb, &ph);
        dc.SelectObject(&Theme::FontSmall());
        dc.SetTextColor(Theme::TextDim());
        dc.DrawText(item.Done ? I18n::T(S_ROW_FAILED) : CString(L"…"), rcThumb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 文件名
    CRect rcName = FileNameRect(index);
    if (m_editIndex == index)
    {
        // 正在编辑,画出编辑占位(实际控件覆盖其上)
        CBrush eb(RGB(0x10, 0x14, 0x1A));
        dc.FillRect(rcName, &eb);
    }
    dc.SelectObject(&Theme::Font());
    dc.SetTextColor(Theme::Text());
    dc.DrawText(item.FileName, rcName, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    // 提示:双击改日期
    dc.SelectObject(&Theme::FontSmall());
    dc.SetTextColor(Theme::TextDim());
    CRect rcSub(rcName.left, rcName.bottom + 1, rcName.right + 120, rc.bottom - 4);
    CString sub;
    if (!item.Done)
        sub = I18n::T(S_PROCESSING);
    else if (!item.Result.Error.IsEmpty())
        sub = item.Result.Error;
    else
        sub.Format(L"%lld KB → %lld KB", item.Result.SrcSize / 1024, item.Result.OutSize / 1024);
    dc.DrawText(sub, rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 警告图标(文字代替图形,简单清晰)
    int iconX = rc.right - 30;
    dc.SelectObject(&Theme::Font());
    if (item.Result.Warn & WarnStillBig)
    {
        dc.SetTextColor(RGB(0xE8, 0x50, 0x50)); // 红
        dc.DrawText(L"②", CRect(iconX, rc.top, iconX + 24, rc.bottom),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        iconX -= 26;
    }
    if (item.Result.Warn & WarnLargeSrc)
    {
        dc.SetTextColor(RGB(0xE8, 0xB8, 0x40)); // 黄
        dc.DrawText(L"①", CRect(iconX, rc.top, iconX + 24, rc.bottom),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        iconX -= 26;
    }
    if (item.Result.Warn & WarnUnsupported)
    {
        dc.SetTextColor(RGB(0xE8, 0x50, 0x50));
        dc.DrawText(L"✕", CRect(iconX, rc.top, iconX + 24, rc.bottom),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 分隔线
    CPen pen(PS_SOLID, 1, RGB(0x10, 0x14, 0x1A));
    CPen* op = dc.SelectObject(&pen);
    dc.MoveTo(rc.left, rc.bottom - 1);
    dc.LineTo(rc.right, rc.bottom - 1);
    dc.SelectObject(op);

    dc.Detach();
}

// ---------------------------------------------------------------------------
// 双击文件名 → 日期编辑(列表 LBN_DBLCLK 通知)
// ---------------------------------------------------------------------------

void CImportDlg::OnListDblClk()
{
    // 列表双击通知(LBN_DBLCLK):用当前双击位置判断命中行与文件名区域
    DWORD pos = ::GetMessagePos();
    CPoint screenPt(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
    CPoint listPt = screenPt;
    m_list.ScreenToClient(&listPt);

    BOOL outside = FALSE;
    int idx = m_list.ItemFromPoint(listPt, outside);

    wchar_t dbg[256];
    swprintf_s(dbg, L"[导入] 列表双击: listPt=(%d,%d) idx=%d outside=%d\n",
               listPt.x, listPt.y, idx, outside ? 1 : 0);
    ::OutputDebugString(dbg);

    if (outside || idx < 0 || idx >= static_cast<int>(m_items.size()))
        return;

    if (FileNameRect(idx).PtInRect(listPt))
    {
        ::OutputDebugString(L"[导入] 命中文件名区域,进入日期编辑\n");
        BeginDateEdit(idx);
    }
    else
    {
        ::OutputDebugString(L"[导入] 未命中文件名区域\n");
    }
}

void CImportDlg::BeginDateEdit(int index)
{
    CommitDateEdit(); // 先提交上一个

    m_editIndex = index;
    Item& item = *m_items[index];

    CRect rcName = FileNameRect(index);
    // 列表客户区坐标 → 对话框客户区坐标
    CPoint tl(rcName.left, rcName.top);
    m_list.ClientToScreen(&tl);
    ScreenToClient(&tl);
    CRect rcDlg(tl.x, tl.y, tl.x + 240, tl.y + 26);

    if (!m_dateCtrlActive)
    {
        // 用自定义格式同时显示日期与时间(默认样式 + SetFormat)
        m_dateCtrl.Create(WS_CHILD | WS_VISIBLE | DTS_UPDOWN,
                          rcDlg, this, IDC_IMP_DATE);
        m_dateCtrl.SetFormat(L"yyyy-MM-dd HH:mm:ss");
        m_dateCtrl.SetFont(&Theme::Font());
        m_dateCtrlActive = true;
    }
    else
    {
        m_dateCtrl.MoveWindow(rcDlg);
        m_dateCtrl.ShowWindow(SW_SHOW);
    }

    m_dateCtrl.SetTime(item.Time);
    m_dateCtrl.SetFocus();
    m_list.InvalidateRect(FileNameRect(index), FALSE);
}

void CImportDlg::CommitDateEdit()
{
    if (!m_dateCtrlActive || m_editIndex < 0)
        return;

    COleDateTime t;
    if (m_dateCtrl.GetTime(t) == GDT_VALID)
    {
        Item& item = *m_items[m_editIndex];
        item.Time = t;
        // 以新时间重新生成唯一文件名(释放旧名)
        item.FileName = m_nameGen->Regenerate(t, item.FileName);
    }

    m_dateCtrl.ShowWindow(SW_HIDE);
    m_editIndex = -1;
    m_list.Invalidate(FALSE);
}

void CImportDlg::CancelDateEdit()
{
    if (m_dateCtrlActive)
    {
        m_dateCtrl.ShowWindow(SW_HIDE);
        m_editIndex = -1;
        m_list.Invalidate(FALSE);
    }
}

BOOL CImportDlg::PreTranslateMessage(MSG* pMsg)
{
    // 日期编辑激活时: Enter 提交, Esc 取消, 点击控件外提交
    if (m_dateCtrlActive && m_editIndex >= 0)
    {
        if (pMsg->message == WM_KEYDOWN)
        {
            if (pMsg->wParam == VK_RETURN)
            {
                CommitDateEdit();
                return TRUE;
            }
            if (pMsg->wParam == VK_ESCAPE)
            {
                CancelDateEdit();
                return TRUE;
            }
        }
        else if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_LBUTTONDBLCLK)
        {
            // 点击落在日期控件之外则提交
            CPoint pt(pMsg->pt);
            CRect rcDate;
            m_dateCtrl.GetWindowRect(rcDate);
            if (!rcDate.PtInRect(pt))
                CommitDateEdit();
        }
    }
    return CDialog::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------------------
// 完成 / 取消 / 写盘
// ---------------------------------------------------------------------------

void CImportDlg::OnBnClickedDone()
{
    CommitDateEdit();

    // 统计可导入项
    int ready = 0;
    for (const auto& it : m_items)
    {
        if (it->Done && it->Result.Error.IsEmpty() && !it->Result.TmpPath.IsEmpty())
            ++ready;
    }
    if (ready == 0)
    {
        MessageBox(I18n::T(S_NOTHING), I18n::T(S_TIP), MB_ICONINFORMATION);
        return;
    }

    // 二次确认
    CString msg;
    msg.Format(I18n::T(S_CONFIRM_MSG),
               ready, static_cast<LPCTSTR>(m_gameName), static_cast<LPCTSTR>(m_gameDir));
    if (MessageBox(msg, I18n::T(S_CONFIRM_CAP), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    if (DoImport())
    {
        m_imported = true;
        EndDialog(IDOK);
    }
}

void CImportDlg::OnBnClickedCancel()
{
    CancelDateEdit();
    EndDialog(IDCANCEL);
}

bool CImportDlg::DoImport()
{
    CString thumbDir = m_gameDir + L"\\thumbnails";
    ::CreateDirectory(thumbDir, nullptr);

    int okCount = 0, failCount = 0;
    std::vector<CString> written; // 已写入,失败回滚用

    for (const auto& it : m_items)
    {
        if (!it->Done || !it->Result.Error.IsEmpty() || it->Result.TmpPath.IsEmpty())
            continue;

        CString dst = m_gameDir + L"\\" + it->FileName;

        // 移动 tmp → screenshots\(覆盖保护:不应重名,ShotNameGen 已避让)
        if (!::MoveFile(it->Result.TmpPath, dst))
        {
            ++failCount;
            continue;
        }
        written.push_back(dst);

        // 生成 thumbnails\缩略图
        CString thumbPath = thumbDir + L"\\" + it->FileName;
        if (!ImageImporter::MakeThumbnail(dst, thumbPath))
        {
            // 缩略图失败不算致命,网格会回退原图;但记录一下
        }
        ++okCount;
    }

    if (failCount > 0)
    {
        CString msg;
        msg.Format(I18n::T(S_RESULT_FMT), okCount, failCount);
        MessageBox(msg, I18n::T(S_RESULT_CAP), MB_ICONWARNING);
    }

    return okCount > 0;
}

void CImportDlg::CleanupTmp()
{
    // 删除 tmp 里遗留的 imp*.jpg(取消或失败后清理)
    if (m_tmpDir.IsEmpty())
        return;
    CString mask = m_tmpDir + L"\\imp*.jpg";
    WIN32_FIND_DATA fd{};
    HANDLE h = ::FindFirstFile(mask, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            ::DeleteFile(m_tmpDir + L"\\" + fd.cFileName);
    } while (::FindNextFile(h, &fd));
    ::FindClose(h);
}

// ---------------------------------------------------------------------------
// 绘制 / 配色
// ---------------------------------------------------------------------------

BOOL CImportDlg::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(rc);
    pDC->FillRect(rc, &Theme::BkBrush());
    return TRUE;
}

HBRUSH CImportDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (pWnd && pWnd->GetDlgCtrlID() == IDC_IMP_HINT)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(Theme::TextDim());
        return Theme::BkBrush();
    }
    pDC->SetBkColor(Theme::Background());
    pDC->SetTextColor(Theme::Text());
    return Theme::BkBrush();
}

void CImportDlg::OnPaint()
{
    CPaintDC dc(this);
    // 背景由 OnEraseBkgnd 处理,这里只补顶部分隔线
    CPen pen(PS_SOLID, 1, RGB(0x10, 0x14, 0x1A));
    CPen* op = dc.SelectObject(&pen);
    CRect rc;
    GetClientRect(rc);
    dc.MoveTo(rc.left, 40);
    dc.LineTo(rc.right, 40);
    dc.SelectObject(op);
}
