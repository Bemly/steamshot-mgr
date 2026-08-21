#pragma once

#include <afxwin.h>
#include <afxglobals.h>
#include <atlimage.h>
#include <vector>
#include "../core/ScreenshotStore.h"

// ---------------------------------------------------------------------------
// CPreviewDlg —— 全尺寸预览对话框(Steam 风格暗色)
//   * 双击缩略图打开;等比缩放适配窗口
//   * ←/→ 或 A/D 切换上一张/下一张;Esc 关闭
//   * 底部显示:时间戳 · 文件名 · 分辨率 · 序号/总数
// ---------------------------------------------------------------------------
class CPreviewDlg : public CDialog
{
public:
    CPreviewDlg(const std::vector<ScreenshotItem>& shots, size_t index, CWnd* parent = nullptr);

    size_t CurrentIndex() const { return m_index; }

protected:
    BOOL OnInitDialog() override;
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnKeyDown(UINT key, UINT repCnt, UINT flags);
    afx_msg void OnLButtonDown(UINT flags, CPoint pt);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    DECLARE_MESSAGE_MAP()

private:
    const std::vector<ScreenshotItem>& m_shots;
    size_t     m_index;
    CImage     m_image;
    CString    m_status;
    CRect      m_rcImage{}; // 图片实际绘制区域(点击左右半区导航用)

    void LoadCurrent();
    void Prev();
    void Next();
};
