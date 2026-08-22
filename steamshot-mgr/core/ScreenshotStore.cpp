#include "ScreenshotStore.h"
#include "SteamLocator.h"
#include "GameCatalog.h"

#include <algorithm>
#include <unordered_set>

int ScreenshotStore::Scan()
{
    m_games.clear();

    CString root = SteamLocator::GetSteamRoot();
    if (root.IsEmpty())
        return 0;

    // 遍历 userdata 下所有用户: <Steam>\userdata\<UserID>\760\remote
    CString userMask = root + L"\\userdata\\*";
    WIN32_FIND_DATA fdUser{};
    HANDLE hUser = ::FindFirstFile(userMask, &fdUser);
    if (hUser == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (!(fdUser.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fdUser.cFileName[0] == L'.')
            continue;

        CString remote = root + L"\\userdata\\" + fdUser.cFileName + L"\\760\\remote";
        ScanUserDir(remote);
    } while (::FindNextFile(hUser, &fdUser));

    ::FindClose(hUser);

    // 游戏名映射:三级链路 manifest → appinfo.vdf → shortcutnames,
    // 把截图目录涉及的 AppID 作为 wanted 集合传入(appinfo 按需解析)
    std::unordered_set<unsigned int> wanted;
    for (const auto& g : m_games)
        wanted.insert(g.AppId);
    GameCatalog catalog;
    catalog.Load(wanted);
    for (auto& g : m_games)
        g.Name = catalog.GetName(g.AppId);

    // 按截图数量降序排列游戏(截图多的排前面,接近 Steam 管理器观感)
    std::sort(m_games.begin(), m_games.end(),
              [](const GameShots& a, const GameShots& b) { return a.Shots.size() > b.Shots.size(); });

    return static_cast<int>(m_games.size());
}

int ScreenshotStore::TotalCount() const
{
    int total = 0;
    for (const auto& g : m_games)
        total += static_cast<int>(g.Shots.size());
    return total;
}

void ScreenshotStore::ScanUserDir(const CString& remoteDir)
{
    // 从 remoteDir 推导用户与元数据库:
    //   remoteDir = <root>\userdata\<uid>\760\remote
    //   vdf 在 <uid>\760\ 一级,不在 remote 内(docs §2)
    if (remoteDir.GetLength() <= 7 ||
        remoteDir.Right(7).CompareNoCase(L"\\remote") != 0)
        return; // 路径形态不符
    CString dir760  = remoteDir.Left(remoteDir.GetLength() - 7); // ...\userdata\<uid>\760
    int uidPos      = dir760.ReverseFind(L'\\');
    CString userId  = dir760.Mid(uidPos + 1);
    CString vdfPath = dir760 + L"\\screenshots.vdf";

    ScreenshotCloudStatus cloud;
    bool vdfLoaded = cloud.LoadForUser(userId, vdfPath);

    CString mask = remoteDir + L"\\*";
    WIN32_FIND_DATA fd{};
    HANDLE hFind = ::FindFirstFile(mask, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fd.cFileName[0] == L'.')
            continue;

        unsigned int appId = _wtoi(fd.cFileName);
        if (appId == 0)
            continue; // 目录名必须是纯数字 AppID

        GameShots game;
        game.AppId = appId;
        game.Dir   = remoteDir + L"\\" + fd.cFileName + L"\\screenshots";
        ScanGameDir(game.Dir, appId, game, userId, cloud, vdfLoaded);

        if (!game.Shots.empty())
            m_games.push_back(std::move(game));
    } while (::FindNextFile(hFind, &fd));

    ::FindClose(hFind);
}

void ScreenshotStore::ScanGameDir(const CString& screenshotsDir, unsigned int appId,
                                  GameShots& game, const CString& userId,
                                  const ScreenshotCloudStatus& cloud, bool vdfLoaded)
{
    CString mask = screenshotsDir + L"\\*.jpg";
    WIN32_FIND_DATA fd{};
    HANDLE hFind = ::FindFirstFile(mask, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    CString thumbDir = screenshotsDir + L"\\thumbnails\\";

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        ScreenshotItem item;
        item.FileName = fd.cFileName;
        item.FilePath = screenshotsDir + L"\\" + fd.cFileName;

        // 缩略图(若不存在则留空,UI 层回退为解码原图)
        CString thumb = thumbDir + fd.cFileName;
        if (::GetFileAttributes(thumb) != INVALID_FILE_ATTRIBUTES)
            item.ThumbPath = thumb;

        if (!ParseTimestamp(item.FileName, item.Timestamp))
            continue; // 文件名不符合时间戳格式,跳过

        // 云端状态:磁盘为主表左连 vdf(§5),按 basename 小写比对
        std::wstring lower(item.FileName);
        for (auto& c : lower)
            c = towlower(c);
        const CloudEntry* entry = vdfLoaded ? cloud.Query(userId, CString(lower.c_str())) : nullptr;
        if (entry)
        {
            item.Cloud          = entry->State;
            item.PublishedFileId = entry->PublishedFileId;
        }
        else
        {
            // vdf 已加载但无此 basename → 未登记(孤儿);vdf 都没有 → Unknown
            item.Cloud = vdfLoaded ? CloudState::Orphan : CloudState::Unknown;
        }

        game.Shots.push_back(std::move(item));
    } while (::FindNextFile(hFind, &fd));

    ::FindClose(hFind);

    // 按时间降序:最新的在前面
    std::sort(game.Shots.begin(), game.Shots.end(),
              [](const ScreenshotItem& a, const ScreenshotItem& b) { return a.Timestamp > b.Timestamp; });

    // 云端计数(供列表小字展示)
    for (const auto& s : game.Shots)
    {
        if (s.Cloud == CloudState::Uploaded)       ++game.UploadedCount;
        else if (s.Cloud == CloudState::NotUploaded) ++game.NotUploadedCount;
    }
}

bool ScreenshotStore::ParseTimestamp(const CString& fileName, COleDateTime& out)
{
    // 期望格式: YYYYMMDDHHMMSS_N.jpg (共 14 位数字开头)
    if (fileName.GetLength() < 15)
        return false;

    for (int i = 0; i < 14; ++i)
    {
        if (fileName[i] < L'0' || fileName[i] > L'9')
            return false;
    }

    auto get2 = [&](int pos) { return (fileName[pos] - L'0') * 10 + (fileName[pos + 1] - L'0'); };
    auto get4 = [&](int pos) { return get2(pos) * 100 + get2(pos + 2); };

    int year = get4(0);
    int mon  = get2(4);
    int day  = get2(6);
    int hour = get2(8);
    int min  = get2(10);
    int sec  = get2(12);

    COleDateTime ts(year, mon, day, hour, min, sec);
    if (ts.GetStatus() != COleDateTime::valid)
        return false;

    // 合理性校验:Steam 诞生于 2003 年,截图不可能更早
    if (year < 2003 || year > 2100)
        return false;

    out = ts;
    return true;
}
