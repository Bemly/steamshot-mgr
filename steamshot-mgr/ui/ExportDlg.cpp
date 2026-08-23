#include "ExportDlg.h"
#include "Theme.h"
#include "I18n.h"
#include "../resource.h"

#include <process.h>

BEGIN_MESSAGE_MAP(CExportDlg, CDialog)
    ON_WM_DESTROY()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_WM_SIZE()
    ON_BN_CLICKED(IDC_BTN_STOP, OnBtnStop)
    ON_MESSAGE(WM_EXPORT_ONE, OnOneDone)
    ON_MESSAGE(WM_EXPORT_ALL, OnAllFinished)
END_MESSAGE_MAP()

CExportDlg::CExportDlg(const GameShots* game, LPCTSTR ffmpeg, LPCTSTR encoder,
                       LPCTSTR dstRoot, CWnd* parent)
    : CDialog(IDD_EXPORT, parent),
      m_game(game), m_ffmpeg(ffmpeg), m_encoder(encoder), m_dstRoot(dstRoot)
{
    ::InitializeCriticalSection(&m_lock);
}

// ---------------------------------------------------------------------------
// 初始化:建目录 → 生成任务 → 启动调度
// ---------------------------------------------------------------------------

BOOL CExportDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // 标题:导出 — 游戏名
    CString disp;
    if (m_game->Name.IsEmpty())
        disp.Format(L"App %u", m_game->AppId);
    else
        disp = m_game->Name;
    CString title;
    title.Format(I18n::T(S_EXPORT_TITLE), static_cast<LPCTSTR>(disp));
    SetWindowText(title);

    // 窗口 2/5 屏宽、固定比例,居中
    int w = ::GetSystemMetrics(SM_CXSCREEN) * 2 / 5;
    int h = ::GetSystemMetrics(SM_CYSCREEN) * 2 / 5;
    MoveWindow((::GetSystemMetrics(SM_CXSCREEN) - w) / 2,
               (::GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h);

    // 控件
    m_status.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 0, 0), this, IDC_EXP_STATUS);
    m_status.SetFont(&Theme::FontBold());
    m_current.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 0, 0), this, IDC_EXP_CURRENT);
    m_current.SetFont(&Theme::Font());
    m_fails.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                   CRect(0, 0, 0, 0), this, IDC_EXP_FAILS);
    m_fails.SetFont(&Theme::FontSmall());
    m_btnStop.Create(I18n::T(S_STOP_BTN), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     CRect(0, 0, 0, 0), this, IDC_BTN_STOP);
    m_btnStop.SetFont(&Theme::Font());

    // 输出目录:根目录 \ 游戏名(NTFS 转义)
    CString plainName;
    if (m_game->Name.IsEmpty())
        plainName.Format(L"App %u", m_game->AppId);
    else
        plainName = m_game->Name;
    CString dirName = Exporter::SanitizeName(plainName);
    m_dstDir = m_dstRoot + L"\\" + dirName;
    if (!::CreateDirectory(m_dstDir, nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS)
    {
        CString msg;
        msg.Format(I18n::T(S_EXPORT_DIR_FAIL), static_cast<LPCTSTR>(m_dstDir));
        MessageBox(msg, I18n::T(S_EXPORT_CAP), MB_ICONERROR);
        EndDialog(IDCANCEL);
        return TRUE;
    }

    // 生成任务(唯一名,同秒顺延)
    std::unordered_set<std::wstring> used;
    for (const auto& shot : m_game->Shots)
    {
        COleDateTime finalTime;
        CString name = Exporter::MakeUniqueName(shot.Timestamp, m_dstDir, used, finalTime);
        m_tasks.push_back({ shot.FilePath, m_dstDir + L"\\" + name, finalTime });
    }

    // 并发上限 = 逻辑核心数
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    m_maxWorkers = si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 4;

    LayoutControls();
    UpdateProgress();
    StartSched();
    return TRUE;
}

void CExportDlg::LayoutControls()
{
    CRect rc;
    GetClientRect(rc);
    int pad = 12;

    m_status.MoveWindow(pad, pad, rc.Width() - pad * 2, 22);
    m_current.MoveWindow(pad, pad + 26, rc.Width() - pad * 2, 18);
    m_btnStop.MoveWindow(rc.Width() - pad - 110, rc.Height() - pad - 30, 110, 30);
    m_fails.MoveWindow(pad, pad + 52, rc.Width() - pad * 2,
                       rc.Height() - pad * 2 - 52 - 36);
}

void CExportDlg::OnSize(UINT type, int cx, int cy)
{
    CDialog::OnSize(type, cx, cy);
    if (::IsWindow(m_fails.GetSafeHwnd()))
        LayoutControls();
}

// ---------------------------------------------------------------------------
// 任务构建与调度
// ---------------------------------------------------------------------------

void CExportDlg::BuildTasks()
{
    // 已在 OnInitDialog 内联完成
}

void CExportDlg::StartSched()
{
    m_stopEvt = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // CPU 采样基准
    FILETIME idle, kernel, user;
    ::GetSystemTimes(&idle, &kernel, &user);
    auto to64 = [](const FILETIME& ft) {
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        return u.QuadPart; };
    m_prevIdle = to64(idle); m_prevKernel = to64(kernel); m_prevUser = to64(user);

    m_sched = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, SchedProc, this, 0, nullptr));
}

double CExportDlg::SampleCpu()
{
    FILETIME idle, kernel, user;
    if (!::GetSystemTimes(&idle, &kernel, &user))
        return 0;
    auto to64 = [](const FILETIME& ft) {
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        return u.QuadPart; };

    ULONGLONG i = to64(idle), k = to64(kernel), u = to64(user);
    ULONGLONG di = i - m_prevIdle, dk = k - m_prevKernel, du = u - m_prevUser;
    m_prevIdle = i; m_prevKernel = k; m_prevUser = u;

    ULONGLONG total = dk + du;
    if (total == 0)
        return 0;
    return 1.0 - static_cast<double>(di) / static_cast<double>(total);
}

bool CExportDlg::IsCancelled()
{
    return ::WaitForSingleObject(m_stopEvt, 0) == WAIT_OBJECT_0;
}

void CExportDlg::SpawnWorkersIfNeeded()
{
    // active < target 且还有任务 → 补 worker
    ::EnterCriticalSection(&m_lock);
    LONG active = m_active;
    bool hasPending = m_next < m_tasks.size();
    ::LeaveCriticalSection(&m_lock);

    if (active < m_target && hasPending)
    {
        HANDLE h = reinterpret_cast<HANDLE>(
            _beginthreadex(nullptr, 0, WorkerProc, this, 0, nullptr));
        if (h)
        {
            ::EnterCriticalSection(&m_lock);
            m_workers.push_back(h);
            ::LeaveCriticalSection(&m_lock);
        }
    }
}

unsigned __stdcall CExportDlg::SchedProc(void* arg)
{
    static_cast<CExportDlg*>(arg)->SchedLoop();
    return 0;
}

void CExportDlg::SchedLoop()
{
    // 初始并发 2
    SpawnWorkersIfNeeded();

    // 每 2 秒:采样 CPU → 调整 target → 补 worker → 检查结束
    while (::WaitForSingleObject(m_stopEvt, 2000) == WAIT_TIMEOUT)
    {
        double cpu = SampleCpu();

        ::EnterCriticalSection(&m_lock);
        bool hasPending = m_next < m_tasks.size();
        ::LeaveCriticalSection(&m_lock);

        // 维持刚好满载:<90% 加(不超过核心数),>98% 减(不低于 1)
        if (hasPending && cpu < 0.90 && m_target < m_maxWorkers)
            ::InterlockedIncrement(&m_target);
        else if (cpu > 0.98 && m_target > 1)
            ::InterlockedDecrement(&m_target);

        SpawnWorkersIfNeeded();

        ::EnterCriticalSection(&m_lock);
        bool allDone = (m_done + m_failed >= static_cast<int>(m_tasks.size()));
        ::LeaveCriticalSection(&m_lock);
        if (allDone)
            break;
    }

    // 收尾:通知 UI 汇总(正常完成与取消都从这里出)
    PostMessage(WM_EXPORT_ALL);
}

unsigned __stdcall CExportDlg::WorkerProc(void* arg)
{
    static_cast<CExportDlg*>(arg)->WorkerLoop();
    return 0;
}

void CExportDlg::WorkerLoop()
{
    ::InterlockedIncrement(&m_active);

    for (;;)
    {
        // 取消 → 退出
        if (IsCancelled())
            break;

        // 并发收缩:active 超过 target 时本 worker 主动退出
        ::EnterCriticalSection(&m_lock);
        if (m_active > m_target || m_next >= m_tasks.size())
        {
            ::LeaveCriticalSection(&m_lock);
            break;
        }
        Task t = m_tasks[m_next++];
        ::LeaveCriticalSection(&m_lock);

        // 运行 ffmpeg(拿句柄以便取消时终止)
        HANDLE proc = nullptr;
        bool ok = Exporter::RunFfmpeg(m_ffmpeg, m_encoder, t.Src, t.Dst, &proc);
        if (ok && proc)
        {
            // 等进程结束或取消
            HANDLE hs[2] = { proc, m_stopEvt };
            DWORD w = ::WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0 + 1) // 取消:终止进程
            {
                ::TerminateProcess(proc, 1);
                ::WaitForSingleObject(proc, 5000);
                ok = false;
            }
            else
            {
                DWORD ec = 0;
                ::GetExitCodeProcess(proc, &ec);
                ok = (ec == 0) && ::PathFileExists(t.Dst) != FALSE;
            }
            ::CloseHandle(proc);
        }

        // 成功则把文件时间设为文件名时间
        if (ok)
            Exporter::SetFileTimes(t.Dst, t.Time);

        ::EnterCriticalSection(&m_lock);
        if (ok) ++m_done; else ++m_failed;
        ::LeaveCriticalSection(&m_lock);

        PostMessage(WM_EXPORT_ONE, ok ? 1 : 0,
                    reinterpret_cast<LPARAM>(new CString(t.Src)));
    }

    ::InterlockedDecrement(&m_active);
}

void CExportDlg::ShutdownThreads()
{
    if (m_stopEvt)
        ::SetEvent(m_stopEvt); // 通知调度与 worker 退出

    // 等全部线程结束
    if (!m_workers.empty())
    {
        ::WaitForMultipleObjects(static_cast<DWORD>(m_workers.size()),
                                 m_workers.data(), TRUE, 30000);
        for (HANDLE h : m_workers)
            if (h) ::CloseHandle(h);
        m_workers.clear();
    }
    if (m_sched)
    {
        ::WaitForSingleObject(m_sched, 30000);
        ::CloseHandle(m_sched);
        m_sched = nullptr;
    }
}

// ---------------------------------------------------------------------------
// UI 事件
// ---------------------------------------------------------------------------

void CExportDlg::OnBtnStop()
{
    m_cancelled = true;
    ::SetEvent(m_stopEvt); // worker 与调度线程自行收尾,WM_EXPORT_ALL 汇总
    m_btnStop.EnableWindow(FALSE);
    m_status.SetWindowText(I18n::T(S_EXPORT_CAP) + L"…");
}

LRESULT CExportDlg::OnOneDone(WPARAM wParam, LPARAM lParam)
{
    bool ok = wParam != 0;
    auto* src = reinterpret_cast<CString*>(lParam);

    if (!ok && src)
    {
        // 失败清单:文件名 — 原因(取消中统一记"已取消")
        CString line;
        line.Format(L"%s — %s",
                    static_cast<LPCTSTR>(src->Mid(src->ReverseFind(L'\\') + 1)),
                    static_cast<LPCTSTR>(IsCancelled()
                        ? I18n::T(S_STOP_BTN) : I18n::T(S_ROW_FAILED)));
        m_fails.AddString(line);
    }
    delete src;

    UpdateProgress();
    return 0;
}

LRESULT CExportDlg::OnAllFinished(WPARAM, LPARAM)
{
    ShutdownThreads();

    CString msg;
    msg.Format(I18n::T(m_cancelled ? S_EXPORT_CANCELLED : S_EXPORT_DONE),
               m_done, m_failed);
    MessageBox(msg, I18n::T(S_EXPORT_CAP), MB_ICONINFORMATION);
    EndDialog(IDOK);
    return 0;
}

void CExportDlg::UpdateProgress()
{
    CString s;
    s.Format(I18n::T(S_EXPORT_STATUS), m_done + m_failed,
             static_cast<int>(m_tasks.size()));
    m_status.SetWindowText(s);

    // 当前文件:下一个待分发任务
    ::EnterCriticalSection(&m_lock);
    bool has = m_next < m_tasks.size();
    CString next = has ? m_tasks[m_next].Src : CString();
    ::LeaveCriticalSection(&m_lock);

    if (has)
    {
        CString cur;
        cur.Format(I18n::T(S_EXPORT_CURRENT),
                   static_cast<LPCTSTR>(next.Mid(next.ReverseFind(L'\\') + 1)));
        m_current.SetWindowText(cur);
    }
    else
    {
        m_current.SetWindowText(L"");
    }
}

void CExportDlg::OnDestroy()
{
    ShutdownThreads();
    CDialog::OnDestroy();
}

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------

BOOL CExportDlg::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(rc);
    pDC->FillRect(rc, &Theme::BkBrush());
    return TRUE;
}

HBRUSH CExportDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT /*nCtlColor*/)
{
    UINT id = pWnd ? pWnd->GetDlgCtrlID() : 0;
    if (id == IDC_EXP_STATUS || id == IDC_EXP_CURRENT)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(id == IDC_EXP_STATUS ? Theme::Text() : Theme::TextDim());
        return Theme::BkBrush();
    }
    if (id == IDC_EXP_FAILS)
    {
        pDC->SetBkColor(Theme::Panel());
        pDC->SetTextColor(Theme::TextDim());
        return Theme::PanelBrush();
    }
    pDC->SetBkColor(Theme::Background());
    pDC->SetTextColor(Theme::Text());
    return Theme::BkBrush();
}
