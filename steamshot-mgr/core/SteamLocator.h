#pragma once

#include <afx.h>
#include <atlstr.h>
#include <vector>

// ---------------------------------------------------------------------------
// SteamLocator —— 负责定位 Steam 安装路径与所有游戏库目录(只读)
//
// 数据来源:
//   1. 注册表 HKCU\Software\Valve\Steam 的 SteamPath 值
//   2. <Steam>\steamapps\libraryfolders.vdf 中登记的附加库目录
// ---------------------------------------------------------------------------
class SteamLocator
{
public:
    // 从注册表读取 Steam 安装根目录,失败返回空串
    static CString GetSteamRoot();

    // 返回所有库目录下的 steamapps 路径(含主库),例如:
    //   C:\Program Files (x86)\Steam\steamapps
    //   D:\SteamLibrary\steamapps
    static std::vector<CString> GetSteamAppsDirs();
};
