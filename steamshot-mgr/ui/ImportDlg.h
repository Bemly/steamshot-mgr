#pragma once

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxcmn.h>
#include <afxdtctl.h>
#include <atlimage.h>
#include <atlcomtime.h>
#include <memory>
#include <vector>
#include "../core/ImageImporter.h"
#include "../core/ShotNameGen.h"

// ---------------------------------------------------------------------------
// CImportDlg —— 导入对话框(暗色,Steam 风格)
//
// 布局:
//   顶部提示条: "拖入图片到下方列表,或点击 [选取图片]"
//   中部:     自绘列表(缩略图 | 生成文件名 | 大小 | 警告图标)
//             · 支持从资源管理器拖入文件(DragAcceptFiles)
//             · 双击文件名 → 原位弹出 CDateTimeCtrl 改日期(即改文件名)
//   底部:     [完成]  [取消]
//
// 流程: 加入图片 → 后台线程 Analyze(转JPG/压缩,落盘 tmp) → 列表逐行刷新
//       完成 → 二次确认 → 搬到 screenshots\ + 生成 thumbnails\
// ---------------------------------------------------------------------------

// 后台完成一张后投递
#define WM_IMPORT_DONE (WM_APP + 201)

class CImportDlg : public CDialog
{
public:
    // gameDir: 该游戏 screenshots\ 目录;gameName: 显示用游戏名
    CImportDlg(LPCTSTR gameDir, LPCTSTR gameName, CWnd* parent = nullptr);

    // 导入是否已确认并完成写盘(供调用方决定是否刷新)
    bool Imported() const { return m_imported; }

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnDestroy();
    afx_msg void OnDropFiles(HDROP hDrop);
    afx_msg void OnBnClickedBrowse();
    afx_msg void OnBnClickedDone();
    afx_msg void OnBnClickedCancel();
    afx_msg void OnListDblClk();   // 列表 LBN_DBLCLK 通知(双击文件名改日期)
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg LRESULT OnImportDone(WPARAM wParam, LPARAM lParam);
    afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMis);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis);
    DECLARE_MESSAGE_MAP()

private:
    // ---- 列表项 ----
    struct Item
    {
        CString              SrcPath;    // 原始图片
        CString              FileName;   // 生成的目标文件名(可改)
        COleDateTime         Time;       // 当前时间戳
        ImportResult         Result;     // 转换结果(含 tmp 路径/大小/警告)
        bool                 Done = false; // 后台是否处理完
        std::unique_ptr<CImage> Thumb;   // 列表小缩略图
    };

    CString m_gameDir;
    CString m_gameName;
    CString m_tmpDir;                 // 760\remote\tmp
    std::vector<std::unique_ptr<Item>> m_items;
    std::unique_ptr<ShotNameGen> m_nameGen;
    bool m_imported = false;

    // 控件
    CListBox m_list;
    CButton  m_btnBrowse;
    CButton  m_btnDone;
    CButton  m_btnCancel;
    CStatic  m_hint;

    // 双击日期编辑(弹出 CDateEditDlg 子对话框,确定后才改文件名)
    int m_editIndex = -1;   // 正在编辑日期的行(仅用于编辑后重绘定位)

    // 后台批处理
    HANDLE m_thread  = nullptr;
    HANDLE m_wakeEvt = nullptr;
    HANDLE m_stopEvt = nullptr;
    CRITICAL_SECTION m_lock{};
    std::vector<int> m_pending; // 待处理索引

    void AddFiles(const std::vector<CString>& paths);
    void StartWorker();
    void StopWorker();
    static unsigned int __stdcall WorkerProc(void* arg);
    void WorkerLoop();

    void DrawListItem(LPDRAWITEMSTRUCT lpDis);
    CRect FileNameRect(int index) const;       // 文件名区域(双击定位)
    void BeginDateEdit(int index);             // 弹出日期编辑对话框并应用结果

    bool DoImport();     // 二次确认 + 写盘;成功返回 true
    void CleanupTmp();   // 清空 tmp 目录残留
    void LayoutControls();

    static constexpr int kRowH = 56; // 行高(含缩略图)
};

// ---------------------------------------------------------------------------
// CDateEditDlg —— 修改日期时间的小对话框(IDD_DATE_EDIT)
//   DTP 控件 + [确定][取消];确定时把控件值写回 m_time
// ---------------------------------------------------------------------------
class CDateEditDlg : public CDialog
{
public:
    COleDateTime m_time;   // 进入时为原时间,确定后被控件新值覆盖

    explicit CDateEditDlg(COleDateTime t, CWnd* parent = nullptr);

protected:
    BOOL OnInitDialog() override;
    afx_msg void OnDtpKillFocus(NMHDR* hdr, LRESULT* res);
    DECLARE_MESSAGE_MAP()
};
