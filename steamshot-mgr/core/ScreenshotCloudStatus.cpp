#include "ScreenshotCloudStatus.h"
#include "VdfParser.h"

#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

CString ScreenshotCloudStatus::MakeKey(const CString& userId, const CString& fileNameLower)
{
    CString k(userId);
    k += L'|';
    k += fileNameLower;
    std::wstring w(k);
    for (auto& c : w)
        c = towlower(c); // 双保险:键整体小写
    return CString(w.c_str());
}

// vdf 的 filename 形如 "1365730/screenshots/20260313040754_1.jpg"(正斜杠),
// 取末段 basename 并转小写,与磁盘文件名比对
CString ScreenshotCloudStatus::BasenameLower(const CString& vdfFileName)
{
    LPCTSTR base = ::PathFindFileName(vdfFileName);
    std::wstring w(base);
    for (auto& c : w)
        c = towlower(c);
    return CString(w.c_str());
}

bool ScreenshotCloudStatus::LoadForUser(const CString& userId, const CString& vdfPath)
{
    // 先清掉该用户旧条目(重复加载/刷新场景)
    CString prefix = userId + L'|';
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (it->first.compare(0, prefix.GetLength(), prefix) == 0)
            it = m_entries.erase(it);
        else
            ++it;
    }

    auto doc = ParseVdfFile(vdfPath);
    if (!doc)
        return false; // 整用户 Unknown,不部分采用

    const VdfNode* root = doc->Find(L"screenshots");
    if (!root)
        return true; // 文件合法但无记录区,视为空库

    for (const auto& gameBlock : root->Children) // depth1: 键=gameid
    {
        for (const auto& entryNode : gameBlock->Children) // depth2: 键=序号
        {
            // filename 是比对主键;缺失则跳过该条
            CString fileName = entryNode->GetString(L"filename");
            if (fileName.IsEmpty())
                continue;

            CloudEntry e;

            // 判定式:字符串原样比较,严禁数值转换(§4.3 大整数陷阱)
            CString pub = entryNode->GetString(L"publishedfileid");
            pub.Trim();
            CString hs = entryNode->GetString(L"hscreenshot");
            hs.Trim();

            const bool hasPid = (!pub.IsEmpty() && pub != L"0");
            const bool hsOk   = (hs != kHsScreenshotSentinel);

            if (hasPid && hsOk)
            {
                e.State = CloudState::Uploaded;
                e.PublishedFileId = pub;
            }
            else if (!hasPid && !hsOk)
            {
                e.State = CloudState::NotUploaded;
            }
            else
            {
                // 半同步状态(理论不应出现):保守视为未上传
                e.State = CloudState::NotUploaded;
            }
            m_entries[std::wstring(MakeKey(userId, BasenameLower(fileName)))] = std::move(e);
        }
    }
    return true;
}

const CloudEntry* ScreenshotCloudStatus::Query(const CString& userId,
                                               const CString& fileNameLower) const
{
    CString key = MakeKey(userId, fileNameLower);
    std::wstring w(key);
    auto it = m_entries.find(w);
    return it != m_entries.end() ? &it->second : nullptr;
}
