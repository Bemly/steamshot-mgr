#pragma once

#include <afxwin.h>
#include <atlimage.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "../core/ScreenshotStore.h"

// ---------------------------------------------------------------------------
// ThumbGridView —— 右侧缩略图网格视图(自绘 + 虚拟化滚动 + 后台解码)
//
// 设计要点:
//   * 不一次性加载 3000+ 张图,仅解码可视区附近的缩略图
//   * 优先读取 Steam 自带的 thumbnails/ 缩略图,缺失时解码原图
//   * 后台线程池解码,CImage 经 GDI+ 缩放后存入 LRU 内存缓存
//   * 只读:不改动 Steam 目录内任何文件,缓存全部驻留内存
// ---------------------------------------------------------------------------

// 自定义消息:后台线程完成一张缩略图后投递给视图
#define WM_THUMB_READY (WM_APP + 101)

class ThumbGridView : public CWnd
{
public:
    ThumbGridView();
    ~ThumbGridView() override;

    BOOL Create(CWnd* parent, UINT id);

    // 设置当前显示的游戏;nullptr 清空
    void SetGame(const GameShots* game);

protected:
    afx_msg int     OnCreate(LPCREATESTRUCT cs);
    afx_msg void    OnDestroy();
    afx_msg void    OnPaint();
    afx_msg void    OnSize(UINT type, int cx, int cy);
    afx_msg void    OnVScroll(UINT code, UINT pos, CScrollBar* bar);
    afx_msg BOOL    OnMouseWheel(UINT flags, short delta, CPoint pt);
    afx_msg void    OnLButtonDown(UINT flags, CPoint pt);
    afx_msg void    OnLButtonDblClk(UINT flags, CPoint pt);
    afx_msg BOOL    OnEraseBkgnd(CDC* pDC);
    afx_msg LRESULT OnThumbReady(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    // ---- 布局常量 ----
    static constexpr int kCellW   = 220;  // 单元格宽(含间距)
    static constexpr int kCellH   = 168;  // 单元格高(含时间标签)
    static constexpr int kThumbH  = 120;  // 缩略图绘制区高度
    static constexpr int kMargin  = 12;   // 外边距
    static constexpr int kMaxCache = 300; // 内存缓存上限(LRU)

    const GameShots* m_game = nullptr;
    int m_cols      = 1;
    int m_scrollPos = 0;    // 垂直滚动像素
    int m_totalH    = 0;    // 内容总高度
    int m_selected  = -1;   // 当前选中项索引

    // ---- 缩略图缓存(LRU)----
    struct CacheEntry
    {
        std::unique_ptr<CImage> Image;
        UINT64 LastUse = 0;
    };
    std::unordered_map<int, CacheEntry> m_cache; // key = 截图索引
    UINT64 m_clock = 0;

    // ---- 后台解码线程 ----
    HANDLE m_thread  = nullptr;
    HANDLE m_wakeEvt = nullptr;  // 唤醒解码线程
    HANDLE m_stopEvt = nullptr;  // 通知线程退出
    CRITICAL_SECTION m_queueLock{};
    std::vector<int>      m_pending;   // 待解码索引(优先级:靠后先解码)
    std::unordered_set<int> m_queued;  // 已在队列中的索引
    unsigned int          m_requestGen = 0; // 代次,切换游戏时使旧任务作废

    void StartWorker();
    void StopWorker();
    static unsigned int __stdcall WorkerProc(void* arg);
    void WorkerLoop();

    void RequestThumb(int index);          // 请求某索引的缩略图(去重)
    const CImage* GetThumb(int index);     // 查缓存
    void EvictCache();                     // LRU 淘汰
    void ClearCacheAndQueue();

    void RecalcLayout();
    int  HitTest(CPoint pt) const;
    void EnsureVisible(int index);
    void OpenPreview(int index);
};
