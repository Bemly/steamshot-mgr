#include "GameCatalog.h"
#include "SteamLocator.h"
#include "VdfParser.h"
#include "AppInfoDb.h"
#include "ShortcutNames.h"

void GameCatalog::Load(const std::unordered_set<unsigned int>& wanted)
{
    m_names.clear();

    // 1) 已安装游戏: appmanifest_*.acf
    LoadManifests();

    // 2) 未安装但曾拥有/浏览: appinfo.vdf(只查 wanted 里还没解出的)
    std::unordered_set<unsigned int> missing;
    for (unsigned int id : wanted)
    {
        if (!m_names.count(id))
            missing.insert(id);
    }
    if (!missing.empty())
        LoadAppInfo(missing);

    // 3) 非 Steam 游戏: shortcutnames
    LoadShortcutNames();
}

void GameCatalog::LoadManifests()
{
    for (const CString& appsDir : SteamLocator::GetSteamAppsDirs())
    {
        CString mask = appsDir + L"\\appmanifest_*.acf";
        WIN32_FIND_DATA fd{};
        HANDLE hFind = ::FindFirstFile(mask, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        do
        {
            // 文件名形如 appmanifest_1447430.acf,提取其中的 AppID
            CString name(fd.cFileName);
            int start = name.Find(L'_');
            int end = name.ReverseFind(L'.');
            if (start < 0 || end <= start)
                continue;

            unsigned int appId = _wtoi(name.Mid(start + 1, end - start - 1));
            if (appId == 0)
                continue;

            ParseManifest(appsDir + L"\\" + name, appId);
        } while (::FindNextFile(hFind, &fd));

        ::FindClose(hFind);
    }
}

void GameCatalog::ParseManifest(LPCTSTR acfPath, unsigned int appId)
{
    auto doc = ParseVdfFile(acfPath);
    if (!doc)
        return;

    const VdfNode* appState = doc->Find(L"AppState");
    if (!appState)
        return;

    CString gameName = appState->GetString(L"name");
    gameName.Trim();
    if (!gameName.IsEmpty())
        m_names[appId] = gameName;
}

void GameCatalog::LoadAppInfo(const std::unordered_set<unsigned int>& missing)
{
    AppInfoDb db;
    std::unordered_map<unsigned int, CString> resolved;
    if (db.LoadWanted(missing, resolved) > 0)
    {
        for (const auto& kv : resolved)
            m_names[kv.first] = kv.second;
    }
}

void GameCatalog::LoadShortcutNames()
{
    std::unordered_map<unsigned int, CString> shortcuts;
    ShortcutNames loader;
    loader.Load(shortcuts);

    for (const auto& kv : shortcuts)
    {
        // 只补缺,manifest/appinfo 优先级更高
        if (!m_names.count(kv.first))
            m_names[kv.first] = kv.second;
    }
}

CString GameCatalog::GetName(unsigned int appId) const
{
    auto it = m_names.find(appId);
    return it != m_names.end() ? it->second : CString();
}
