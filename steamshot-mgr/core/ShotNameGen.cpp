#include "ShotNameGen.h"

#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

namespace
{
    std::wstring ToLower(const CString& s)
    {
        std::wstring w(s);
        for (auto& c : w)
            c = towlower(c);
        return w;
    }
}

ShotNameGen::ShotNameGen(LPCTSTR targetDir) : m_targetDir(targetDir)
{
}

CString ShotNameGen::Format(const COleDateTime& t)
{
    CString s;
    s.Format(L"%04d%02d%02d%02d%02d%02d_1.jpg",
             t.GetYear(), t.GetMonth(), t.GetDay(),
             t.GetHour(), t.GetMinute(), t.GetSecond());
    return s;
}

bool ShotNameGen::ExistsOnDisk(const CString& name) const
{
    CString full = m_targetDir + L"\\" + name;
    return ::PathFileExists(full) != FALSE;
}

bool ShotNameGen::IsTaken(const CString& name) const
{
    if (m_used.find(ToLower(name)) != m_used.end())
        return true;
    return ExistsOnDisk(name);
}

CString ShotNameGen::Generate(const COleDateTime& mtime)
{
    COleDateTime t = mtime;
    // 同秒冲突 → 秒数 +1 继续,直到唯一(最多尝试 86400 秒防死循环)
    for (int i = 0; i < 86400; ++i)
    {
        CString name = Format(t);
        if (!IsTaken(name))
        {
            m_used.insert(ToLower(name));
            return name;
        }
        t += COleDateTimeSpan(0, 0, 0, 1); // +1 秒
    }
    // 极端情况:退化为 _1 递增(理论上不会走到)
    CString fallback;
    fallback.Format(L"%s_%d.jpg", static_cast<LPCTSTR>(Format(mtime).Left(14)),
                    static_cast<int>(m_used.size() + 1));
    m_used.insert(ToLower(fallback));
    return fallback;
}

CString ShotNameGen::Regenerate(const COleDateTime& newTime, const CString& oldName)
{
    // 先释放旧名,再以新时间重新生成
    m_used.erase(ToLower(oldName));
    return Generate(newTime);
}
