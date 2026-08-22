#pragma once

#include <afx.h>
#include <atlstr.h>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// GameCatalog —— AppID → 游戏名称映射表(只读)
//
// 三级离线链路(docs/offline-appid-mapping.md 第 1/7 节):
//   1. appmanifest_<AppID>.acf   已安装游戏(文本 VDF,最高优先级)
//   2. appcache/appinfo.vdf      未安装但曾拥有/浏览的游戏(二进制 v41)
//   3. screenshots.vdf shortcutnames  非 Steam 游戏
//   全部未命中 → 调用方回退显示 "App <AppID>"
//
// appinfo.vdf 全量 6 MiB 解析约 1-2 秒,故只按 wanted 集合按需提取。
// ---------------------------------------------------------------------------
class GameCatalog
{
public:
    // 建立名称映射;wanted 为截图目录涉及的 AppID(用于 appinfo 按需过滤)
    void Load(const std::unordered_set<unsigned int>& wanted);

    // 查询游戏名;查不到返回空串
    CString GetName(unsigned int appId) const;

private:
    std::unordered_map<unsigned int, CString> m_names;

    void LoadManifests();
    void ParseManifest(LPCTSTR acfPath, unsigned int appId);
    void LoadAppInfo(const std::unordered_set<unsigned int>& missing);
    void LoadShortcutNames();
};
