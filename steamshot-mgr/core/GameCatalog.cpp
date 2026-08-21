#include "GameCatalog.h"
#include "SteamLocator.h"
#include "VdfParser.h"

void GameCatalog::Load()
{
    m_names.clear();

    for (const CString& appsDir : SteamLocator::GetSteamAppsDirs())
    {
        // 遍历 appmanifest_*.acf
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

CString GameCatalog::GetName(unsigned int appId) const
{
    auto it = m_names.find(appId);
    return it != m_names.end() ? it->second : CString();
}
