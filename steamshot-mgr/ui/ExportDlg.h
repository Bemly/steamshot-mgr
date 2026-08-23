#pragma once

#include <afxwin.h>
#include <atlcomtime.h>
#include <memory>
#include <vector>
#include <unordered_set>
#include "../core/ScreenshotStore.h"
#include "../core/Exporter.h"

// 自定义消息
#define WM_EXPORT_ONE  (WM_APP + 301)  // 一张处理完(wParam:1成功/0失败)
#define WM_EXPORT_ALL  (WM_APP + 302)  // 全部结束(正常完成或取消收尾)

// ---------------------------------------------------------------------------
// CExportDlg —— 批量导出进度对话框(暗色,Steam 风格)
//
// 前置:调用方(MainFrame)已完成 ffmpeg 检测、编码器探测、目录选择。
// 本对话框职责:
//   1. 创建输出子目录(游戏名,NTFS 转义)并生成唯一导出名(同秒顺延)
//   2. 调度线程动态伸缩 ffmpeg 并发:初始 2,按 CPU 利用率(<90% 加,
//      >98% 减)在 1 ~ 逻辑核心数 之间调节,维持刚好满载
//   3. 进度/当前文件/失败列表实时刷新;[取消导出]终止在跑进程并汇总
// ---------------------------------------------------------------------------
class CExportDlg : public CDialog
{
public:
    // game: 当前游戏(含截图列表);ffmpeg/encoder: 已探测的可用项;
    // dstRoot: 用户选择的目标根目录
    CExportDlg(const GameShots* game, LPCTSTR ffmpeg, LPCTSTR encoder,
               LPCTSTR dstRoot, CWnd* parent = nullptr);

protected:
    BOOL OnInitDialog() override;
    afx_msg void OnDestroy();
    afx_msg void OnBtnStop();
    afx_msg LRESULT OnOneDone(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAllFinished(WPARAM wParam, LPARAM lParam);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnSize(UINT type, int cx, int cy);
    DECLARE_MESSAGE_MAP()

private:
    // 任务
    struct Task
    {
        CString       Src;    // 源 jpg 完整路径
        CString       Dst;    // 目标 avif 完整路径
        COleDateTime  Time;   // 文件名时间(同秒顺延后的最终值,用于 SetFileTimes)
    };

    const GameShots* m_game;
    CString m_ffmpeg;
    CString m_encoder;
    CString m_dstRoot;
    CString m_dstDir;          // dstRoot\游戏名(已转义)

    // 控件
    CStatic  m_status;         // "导出中 x / n"
    CStatic  m_current;        // 当前文件
    CListBox m_fails;          // 失败列表
    CButton  m_btnStop;

    std::vector<Task> m_tasks;
    size_t m_next    = 0;      // 下一个待分发索引
    int    m_done    = 0;      // 成功数
    int    m_failed  = 0;      // 失败数
    bool   m_cancelled = false;

    // 并发控制
    HANDLE m_stopEvt  = nullptr; // 取消/退出事件
    HANDLE m_sched    = nullptr; // 调度线程
    CRITICAL_SECTION m_lock{};
    volatile LONG m_active = 0;  // 活跃 worker 数
    volatile LONG m_target = 2;  // 目标并发(动态调整)
    int  m_maxWorkers = 2;       // 上限 = 逻辑核心数
    ULONGLONG m_prevIdle = 0, m_prevKernel = 0, m_prevUser = 0; // CPU 采样基准
    std::vector<HANDLE> m_workers; // worker 线程句柄(收尾等待)

    void BuildTasks();
    void StartSched();
    void ShutdownThreads();    // 等待并回收全部线程
    static unsigned __stdcall SchedProc(void* arg);
    void SchedLoop();
    static unsigned __stdcall WorkerProc(void* arg);
    void WorkerLoop();
    void SpawnWorkersIfNeeded();
    double SampleCpu();        // 返回 0~1
    bool IsCancelled();        // stopEvt 是否已触发
    void UpdateProgress();
    void LayoutControls();
};
