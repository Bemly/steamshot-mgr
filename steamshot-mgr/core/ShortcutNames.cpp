#include "ShortcutNames.h"
#include "SteamLocator.h"
#include "VdfParser.h"

std::vector<CString> ShortcutNames::EnumUserDirs()
{
    std::vector<CString> dirs;
    CString root = SteamLocator::GetSteamRoot();
    if (root.IsEmpty())
        return dirs;

    CString mask = root + L"\\userdata\\*";
    WIN32_FIND_DATA fd{};
    HANDLE hFind = ::FindFirstFile(mask, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return dirs;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fd.cFileName[0] == L'.')
            continue;
        dirs.push_back(root + L"\\userdata\\" + fd.cFileName);
    } while (::FindNextFile(hFind, &fd));

    ::FindClose(hFind);
    return dirs;
}

int ShortcutNames::Load(std::unordered_map<unsigned int, CString>& out)
{
    out.clear();
    int users = 0;

    for (const CString& userDir : EnumUserDirs())
    {
        CString vdf = userDir + L"\\760\\screenshots.vdf";
        if (::GetFileAttributes(vdf) == INVALID_FILE_ATTRIBUTES)
            continue;
        LoadUser(vdf, out);
        ++users;
    }
    return users;
}

void ShortcutNames::LoadUser(LPCTSTR vdfPath, std::unordered_map<unsigned int, CString>& out)
{
    auto doc = ParseVdfFile(vdfPath);
    if (!doc)
        return;

    const VdfNode* shortcuts = doc->Find(L"shortcutnames");
    if (!shortcuts)
        return;

    for (const auto& child : shortcuts->Children)
    {
        unsigned int appId = _wtoi(child->Key);
        if (appId == 0)
            continue; // 键必须是数字 AppID

        CString name(child->Value);
        name.Trim();
        if (name.IsEmpty() || out.count(appId))
            continue; // 多用户时先到先得,不覆盖
        out[appId] = name;
    }
}
