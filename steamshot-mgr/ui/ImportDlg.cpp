#include "ImportDlg.h"
#include "Theme.h"
#include "I18n.h"
#include "../resource.h"

#include <process.h>
#include <cstdarg>
#include <cstdio>

// 诊断日志:同时输出到 VS 输出窗口(OutputDebugString)与临时目录 ssm_dbg.log
// (排查日期编辑问题用,稳定后移除)
static void DbgLog(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);

    ::OutputDebugStringW(buf);
    ::OutputDebugStringW(L"\n");

    wchar_t path[MAX_PATH];
    ::GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"ssm_dbg.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f)
        return;
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    fwprintf(f, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    fwprintf(f, L"%s\n", buf);
    fclose(f);
}

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

    DbgLog(L"双击: listPt=(%d,%d) idx=%d outside=%d", listPt.x, listPt.y, idx, outside ? 1 : 0);

    if (outside || idx < 0 || idx >= static_cast<int>(m_items.size()))
        return;

    if (FileNameRect(idx).PtInRect(listPt))
    {
        DbgLog(L"命中文件名区域, 弹出日期编辑, 行=%d 当前名=%s", idx,
               static_cast<LPCTSTR>(m_items[idx]->FileName));
        BeginDateEdit(idx);
    }
    else
    {
        DbgLog(L"未命中文件名区域");
    }
}

void CImportDlg::BeginDateEdit(int index)
{
    // 弹出"修改日期"子对话框:确定后才改文件名,取消则不动
    Item& item = *m_items[index];
    CString oldName = item.FileName;
    COleDateTime oldTime = item.Time;
    DbgLog(L"BeginDateEdit 进入: index=%d old=%s time=%04d-%02d-%02d %02d:%02d:%02d",
           index, static_cast<LPCTSTR>(oldName),
           oldTime.GetYear(), oldTime.GetMonth(), oldTime.GetDay(),
           oldTime.GetHour(), oldTime.GetMinute(), oldTime.GetSecond());

    CDateEditDlg dlg(item.Time, this);
    INT_PTR ret = dlg.DoModal();
    DbgLog(L"DoModal 返回=%Id, dlg.m_time=%04d-%02d-%02d %02d:%02d:%02d",
           ret,
           dlg.m_time.GetYear(), dlg.m_time.GetMonth(), dlg.m_time.GetDay(),
           dlg.m_time.GetHour(), dlg.m_time.GetMinute(), dlg.m_time.GetSecond());
    if (ret != IDOK)
        return; // 用户取消,文件名保持不变

    item.Time = dlg.m_time;
    // 以新时间重新生成唯一文件名(释放旧名)
    item.FileName = m_nameGen->Regenerate(dlg.m_time, item.FileName);
    DbgLog(L"Regenerate 完成: new=%s", static_cast<LPCTSTR>(item.FileName));

    // 重绘该行
    m_editIndex = index;
    CRect rc = FileNameRect(index);
    rc.right = rc.left + 320; // 覆盖小字区域
    m_list.InvalidateRect(rc, FALSE);
    m_editIndex = -1;
}

// ---------------------------------------------------------------------------
// 完成 / 取消 / 写盘
// ---------------------------------------------------------------------------

void CImportDlg::OnBnClickedDone()
{
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

// ---------------------------------------------------------------------------
// CDateEditDlg —— 修改日期时间的小对话框
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CDateEditDlg, CDialog)
END_MESSAGE_MAP()

CDateEditDlg::CDateEditDlg(COleDateTime t, CWnd* parent)
    : CDialog(IDD_DATE_EDIT, parent), m_time(t)
{
}

BOOL CDateEditDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // 标题与按钮文字走 i18n(资源模板里是中文默认值)
    SetWindowText(I18n::T(S_DATE_TITLE));
    CWnd* ok = GetDlgItem(IDOK);
    CWnd* cancel = GetDlgItem(IDCANCEL);
    if (ok)     ok->SetWindowText(I18n::T(S_BTN_OK));
    if (cancel) cancel->SetWindowText(I18n::T(S_CANCEL));

    // DTP:自定义格式同时显示日期与时间;带下拉日历(资源默认样式)
    auto* dtp = static_cast<CDateTimeCtrl*>(GetDlgItem(IDC_IMP_DATE));
    DbgLog(L"DateEdit OnInitDialog: dtp=%p", static_cast<void*>(dtp ? dtp->GetSafeHwnd() : nullptr));
    if (dtp)
    {
        BOOL fmtOk = dtp->SetFormat(L"yyyy-MM-dd HH:mm:ss");
        dtp->SetFont(&Theme::Font());
        dtp->SetTime(m_time);
        DbgLog(L"DTP SetFormat=%d SetTime 完成", fmtOk);
    }
    return TRUE;
}

void CDateEditDlg::OnOK()
{
    // 确定时把控件当前值读回 m_time
    auto* dtp = static_cast<CDateTimeCtrl*>(GetDlgItem(IDC_IMP_DATE));
    COleDateTime t;
    DWORD gt = dtp ? dtp->GetTime(t) : (DWORD)GDT_ERROR;
    DbgLog(L"OnOK 进入: GetTime=%lu 值=%04d-%02d-%02d %02d:%02d:%02d status=%d",
           gt, t.GetYear(), t.GetMonth(), t.GetDay(),
           t.GetHour(), t.GetMinute(), t.GetSecond(), (int)t.GetStatus());

    // 实测:自定义格式(yyyy-MM-dd HH:mm:ss)下控件可能返回 GDT_NONE(1)
    // 但 SYSTEMTIME 已是用户修改后的有效值,因此状态有效即采纳
    if (gt == GDT_VALID || t.GetStatus() == COleDateTime::valid)
        m_time = t;
    EndDialog(IDOK);
}
