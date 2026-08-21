#pragma once

#include <afx.h>
#include <atlstr.h>
#include <unordered_map>

// ---------------------------------------------------------------------------
// GameCatalog —— AppID → 游戏名称映射表(只读)
//
// 从所有库目录的 appmanifest_<AppID>.acf 中读取 "name" 字段;
// 未安装的游戏查不到名称,调用方应回退显示 "App <AppID>"。
// ---------------------------------------------------------------------------
class GameCatalog
{
public:
    // 扫描所有 steamapps 目录,建立名称映射
    void Load();

    // 查询游戏名;查不到返回空串
    CString GetName(unsigned int appId) const;

private:
    std::unordered_map<unsigned int, CString> m_names;

    void ParseManifest(LPCTSTR acfPath, unsigned int appId);
};
