#pragma once

#include <afx.h>
#include <atlstr.h>
#include <atlcomtime.h>
#include <vector>

// ---------------------------------------------------------------------------
// 数据结构
// ---------------------------------------------------------------------------

// 单张截图
struct ScreenshotItem
{
    CString      FilePath;      // 原图完整路径
    CString      ThumbPath;     // 缩略图完整路径(可能不存在)
    CString      FileName;      // 文件名,如 20260313162413_1.jpg
    COleDateTime Timestamp;     // 由文件名解析出的拍摄时间
    int          Width  = 0;    // 分辨率(读图后填充,可选)
    int          Height = 0;
};

// 一个游戏(AppID)的截图集合
struct GameShots
{
    unsigned int               AppId = 0;
    CString                    Name;       // 游戏名;未知时由 UI 回退显示 "App <id>"
    CString                    Dir;        // screenshots 目录
    std::vector<ScreenshotItem> Shots;     // 已按时间降序(最新在前)
};

// ---------------------------------------------------------------------------
// ScreenshotStore —— 扫描并索引 Steam 截图(只读,不修改任何文件)
//
// 目录结构:
//   <Steam>\userdata\<UserID>\760\remote\<AppID>\screenshots\
//       20260313162413_1.jpg        ← 原图
//       thumbnails\20260313162413_1.jpg  ← Steam 生成的缩略图
//
// 文件名即时间戳: YYYYMMDDHHMMSS_序号.jpg
// ---------------------------------------------------------------------------
class ScreenshotStore
{
public:
    // 扫描所有用户目录,建立索引;返回游戏数量
    int Scan();

    const std::vector<GameShots>& Games() const { return m_games; }

    // 总截图数
    int TotalCount() const;

private:
    std::vector<GameShots> m_games;

    void ScanUserDir(const CString& remoteDir);
    void ScanGameDir(const CString& screenshotsDir, unsigned int appId, GameShots& game);

    // 从文件名解析时间戳,如 "20260313162413_1.jpg" → 2026-03-13 16:24:13
    static bool ParseTimestamp(const CString& fileName, COleDateTime& out);
};
