#include "PreviewDlg.h"
#include "Theme.h"
#include "../resource.h"

BEGIN_MESSAGE_MAP(CPreviewDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_KEYDOWN()
    ON_WM_LBUTTONDOWN()
    ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

CPreviewDlg::CPreviewDlg(const std::vector<ScreenshotItem>& shots, size_t index, CWnd* parent)
    : CDialog(IDD_PREVIEW, parent), m_shots(shots), m_index(index)
{
}

BOOL CPreviewDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    // 无边框大窗口:80% 屏幕大小居中
    int w = ::GetSystemMetrics(SM_CXSCREEN) * 4 / 5;
    int h = ::GetSystemMetrics(SM_CYSCREEN) * 4 / 5;
    MoveWindow((::GetSystemMetrics(SM_CXSCREEN) - w) / 2,
               (::GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h);
    LoadCurrent();
    return TRUE;
}

void CPreviewDlg::LoadCurrent()
{
    const ScreenshotItem& shot = m_shots[m_index];

    m_image.Destroy();
    if (FAILED(m_image.Load(shot.FilePath)) || m_image.IsNull())
    {
        m_status.Format(L"无法加载: %s", static_cast<LPCTSTR>(shot.FileName));
    }
    else
    {
        // 云端状态段:已上传(蓝☁)/未上传(橙↑)/孤儿(?)/未知
        CString cloud;
        switch (shot.Cloud)
        {
        case CloudState::Uploaded:    cloud.Format(L"   ·   \u2601 已上传 (pid %s)",
                                                   static_cast<LPCTSTR>(shot.PublishedFileId)); break;
        case CloudState::NotUploaded: cloud  = L"   ·   ↑ 未上传"; break;
        case CloudState::Orphan:      cloud  = L"   ·   ? 未登记"; break;
        default:                      cloud  = L"   ·   状态未知"; break;
        }

        m_status.Format(L"%s   ·   %s   ·   %d × %d   ·   %zu / %zu%s",
                        static_cast<LPCTSTR>(shot.Timestamp.Format(L"%Y-%m-%d %H:%M:%S")),
                        static_cast<LPCTSTR>(shot.FileName),
                        m_image.GetWidth(), m_image.GetHeight(),
                        m_index + 1, m_shots.size(),
                        static_cast<LPCTSTR>(cloud));
    }
    SetWindowText(shot.FileName);
    Invalidate(FALSE);
}

void CPreviewDlg::Prev()
{
    if (m_index > 0)
    {
        --m_index;
        LoadCurrent();
    }
}

void CPreviewDlg::Next()
{
    if (m_index + 1 < m_shots.size())
    {
        ++m_index;
        LoadCurrent();
    }
}

void CPreviewDlg::OnKeyDown(UINT key, UINT repCnt, UINT flags)
{
    switch (key)
    {
    case VK_LEFT:
    case L'A':
        Prev();
        return;
    case VK_RIGHT:
    case L'D':
        Next();
        return;
    case VK_ESCAPE:
        EndDialog(IDOK);
        return;
    }
    CDialog::OnKeyDown(key, repCnt, flags);
}

void CPreviewDlg::OnLButtonDown(UINT /*flags*/, CPoint pt)
{
    // 点击图片左半区上一张、右半区下一张(贴近 Steam 体验)
    CRect rc;
    GetClientRect(rc);
    if (pt.x < rc.Width() / 2)
        Prev();
    else
        Next();
}

BOOL CPreviewDlg::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;
}

HBRUSH CPreviewDlg::OnCtlColor(CDC* pDC, CWnd* /*pWnd*/, UINT /*nCtlColor*/)
{
    pDC->SetBkColor(Theme::Background());
    pDC->SetTextColor(Theme::Text());
    return Theme::BkBrush();
}

void CPreviewDlg::OnSize(UINT type, int cx, int cy)
{
    CDialog::OnSize(type, cx, cy);
    Invalidate(FALSE);
}

void CPreviewDlg::OnPaint()
{
    CPaintDC dc(this);
    CRect rcClient;
    GetClientRect(rcClient);

    CMemDC memDC(dc, rcClient);
    CDC& dcMem = memDC.GetDC();
    dcMem.FillRect(rcClient, &Theme::BkBrush());

    constexpr int kStatusH = 30;
    CRect rcView(rcClient.left + 8, rcClient.top + 8,
                 rcClient.right - 8, rcClient.bottom - kStatusH - 8);

    if (!m_image.IsNull())
    {
        double scaleX = static_cast<double>(rcView.Width())  / m_image.GetWidth();
        double scaleY = static_cast<double>(rcView.Height()) / m_image.GetHeight();
        double scale  = min(scaleX, scaleY);
        int w = static_cast<int>(m_image.GetWidth()  * scale);
        int h = static_cast<int>(m_image.GetHeight() * scale);
        int x = rcView.left + (rcView.Width()  - w) / 2;
        int y = rcView.top  + (rcView.Height() - h) / 2;
        m_rcImage = CRect(x, y, x + w, y + h);
        m_image.Draw(dcMem.m_hDC, x, y, w, h);
    }

    // 底部状态栏
    CRect rcStatus(rcClient.left, rcClient.bottom - kStatusH, rcClient.right, rcClient.bottom);
    CBrush barBrush(Theme::Panel());
    dcMem.FillRect(rcStatus, &barBrush);
    dcMem.SetBkMode(TRANSPARENT);
    dcMem.SelectObject(&Theme::Font());
    dcMem.SetTextColor(Theme::TextDim());
    rcStatus.DeflateRect(12, 0);
    dcMem.DrawText(m_status, rcStatus, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}
