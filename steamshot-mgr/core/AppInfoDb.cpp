#include "AppInfoDb.h"
#include "SteamLocator.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// appinfo.vdf 二进制解析(按 docs/offline-appid-mapping.md 第 4 节实现)
//
// 头部常量:
//   v41 magic = 0x07564429 (")DV\x07");v40/v39 为 0x07564428/0x07564427
//   v40+ 条目固定区多 20 字节 sha1_binary,故 blob 长度 = size - 60
//   旧版(<v40) blob 长度 = size - 40
// ---------------------------------------------------------------------------

namespace
{
    // 条目固定字段长度:appid4+size4+state4+updated4+token8+sha1_20+change4 (+sha_bin20)
    constexpr size_t kBlobShrinkV40Plus = 60;  // blob = size - 60
    constexpr size_t kBlobShrinkOld     = 40;  // blob = size - 40

    // 小端读取工具(文件字节序为 LE,x86 直接取)
    unsigned int ReadU32(const BYTE* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (unsigned(p[3]) << 24); }
    unsigned long long ReadU64(const BYTE* p)
    {
        return static_cast<unsigned long long>(ReadU32(p)) |
               (static_cast<unsigned long long>(ReadU32(p + 4)) << 32);
    }

    bool ReadFileBytes(LPCTSTR path, std::vector<BYTE>& buf)
    {
        HANDLE h = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 256LL * 1024 * 1024)
        {
            ::CloseHandle(h);
            return false;
        }
        buf.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        BOOL ok = ::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
        ::CloseHandle(h);
        return ok && read == buf.size();
    }

    // UTF-8 → UTF-16
    CStringW Utf8ToWide(const BYTE* data, size_t len)
    {
        int n = ::MultiByteToWideChar(CP_UTF8, 0,
                                      reinterpret_cast<const char*>(data),
                                      static_cast<int>(len), nullptr, 0);
        if (n <= 0)
            return CStringW();
        CStringW w;
        ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data),
                              static_cast<int>(len), w.GetBuffer(n), n);
        w.ReleaseBuffer(n);
        return w;
    }
}

int AppInfoDb::LoadWanted(const std::unordered_set<unsigned int>& wanted,
                          std::unordered_map<unsigned int, CString>& out)
{
    out.clear();
    if (wanted.empty())
        return 0;

    // 定位 <Steam>\appcache\appinfo.vdf
    CString root = SteamLocator::GetSteamRoot();
    if (root.IsEmpty())
        return -1;
    CString path = root + L"\\appcache\\appinfo.vdf";

    std::vector<BYTE> buf;
    if (!ReadFileBytes(path, buf))
        return -1;
    const size_t total = buf.size();
    if (total < 24)
        return -1;

    const BYTE* p   = buf.data();

    // ---- 头 16 字节 ----
    unsigned int magic = ReadU32(p);
    bool v40plus;
    if (magic == 0x07564429u)      v40plus = true;   // v41
    else if (magic == 0x07564428u) v40plus = false;  // v40
    else                           return -1;        // 未支持的版本

    unsigned long long strOff = ReadU64(p + 8);
    if (strOff == 0 || strOff >= total)
        return -1;

    // ---- 字符串表 ----
    const BYTE* sp = p + strOff;
    const BYTE* endAll = p + total;
    if (sp + 4 > endAll)
        return -1;
    unsigned int numStrings = ReadU32(sp);
    sp += 4;

    std::vector<std::string> table;
    table.reserve(numStrings < 100000u ? numStrings : 100000u);
    unsigned int nameIdx = UINT_MAX;
    for (unsigned int i = 0; i < numStrings; ++i)
    {
        const BYTE* e = static_cast<const BYTE*>(::memchr(sp, 0, static_cast<size_t>(endAll - sp)));
        if (!e)
            break; // 表被截断,用已有部分继续
        std::string s(reinterpret_cast<const char*>(sp), static_cast<size_t>(e - sp));
        if (s == "name")
            nameIdx = i;
        table.push_back(std::move(s));
        sp = e + 1;
    }
    if (nameIdx == UINT_MAX)
        return 0; // 没有 name 键,无法提取

    // ---- App 条目区:偏移 16 至 string_table_offset ----
    const BYTE* q     = p + 16;
    const BYTE* qEnd  = p + strOff;
    size_t shrink = v40plus ? kBlobShrinkV40Plus : kBlobShrinkOld;
    int resolved = 0;

    while (q + 8 <= qEnd)
    {
        unsigned int appId = ReadU32(q);
        if (appId == 0)
            break; // 结束标记
        unsigned int size = ReadU32(q + 4);

        // 完整条目 = [q, q+8+size),其中 blob 从 q+8 开始、长 size-shrink
        if (size < shrink || q + 8 + size > qEnd)
            break; // 越界/异常,停止
        if (wanted.count(appId))
        {
            const BYTE* blob    = q + 8;
            size_t      blobLen = size - shrink;
            CStringW name;
            if (ExtractName(blob, blobLen, nameIdx, name) && !name.IsEmpty())
            {
                out[appId] = CString(name);
                ++resolved;
            }
        }
        q += 8 + size;
    }
    return resolved;
}

bool AppInfoDb::ExtractName(const BYTE* blob, size_t len, unsigned int nameIdx, CStringW& out)
{
    // 找 String 节点: 类型字节 0x01 + u32 键索引(nameIdx)
    // 即 needle = 01 xx xx xx 00 (小端)
    BYTE needle[5] = { 0x01 };
    needle[1] = static_cast<BYTE>(nameIdx & 0xFF);
    needle[2] = static_cast<BYTE>((nameIdx >> 8) & 0xFF);
    needle[3] = static_cast<BYTE>((nameIdx >> 16) & 0xFF);
    needle[4] = static_cast<BYTE>((nameIdx >> 24) & 0xFF);

    for (size_t pos = 0; pos + 5 <= len; )
    {
        const void* hit = ::memchr(blob + pos, needle[0], len - pos);
        if (!hit)
            break;
        size_t at = static_cast<size_t>(static_cast<const BYTE*>(hit) - blob);
        if (at + 5 > len)
            break;
        if (::memcmp(blob + at, needle, 5) == 0)
        {
            // 值为 null 结尾 UTF-8
            const BYTE* start = blob + at + 5;
            const BYTE* term  = static_cast<const BYTE*>(
                ::memchr(start, 0, len - (at + 5)));
            if (!term)
                break;
            out = Utf8ToWide(start, static_cast<size_t>(term - start));
            return true;
        }
        pos = at + 1;
    }
    return false;
}
