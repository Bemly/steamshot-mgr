#include "SteamLocator.h"
#include "VdfParser.h"

CString SteamLocator::GetSteamRoot()
{
    // Steam 将安装路径写在当前用户注册表中
    HKEY hKey = nullptr;
    if (::RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0,
                       KEY_READ, &hKey) != ERROR_SUCCESS)
        return CString();

    wchar_t buf[MAX_PATH]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LSTATUS st = ::RegQueryValueEx(hKey, L"SteamPath", nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buf), &size);
    ::RegCloseKey(hKey);

    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return CString();

    CString path(buf);
    path.Replace(L'/', L'\\'); // 注册表内为正斜杠,统一成反斜杠
    path.TrimRight(L'\\');
    return path;
}

std::vector<CString> SteamLocator::GetSteamAppsDirs()
{
    std::vector<CString> dirs;

    CString root = GetSteamRoot();
    if (root.IsEmpty())
        return dirs;

    dirs.push_back(root + L"\\steamapps");

    // 解析 libraryfolders.vdf,收集附加库
    CString libVdf = root + L"\\steamapps\\libraryfolders.vdf";
    auto doc = ParseVdfFile(libVdf);
    if (!doc)
        return dirs;

    const VdfNode* folders = doc->Find(L"libraryfolders");
    if (!folders)
        return dirs;

    for (const auto& entry : folders->Children)
    {
        CString libPath = entry->GetString(L"path");
        if (libPath.IsEmpty())
            continue;
        libPath.Replace(L"\\\\", L"\\"); // vdf 中的转义反斜杠
        libPath.TrimRight(L'\\');

        CString apps = libPath + L"\\steamapps";
        // 避免与主库重复
        bool dup = false;
        for (const auto& d : dirs)
        {
            if (d.CompareNoCase(apps) == 0)
            {
                dup = true;
                break;
            }
        }
        if (!dup)
            dirs.push_back(apps);
    }
    return dirs;
}
