#pragma once

#include <afx.h>
#include <atlstr.h>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// ScreenshotCloudStatus —— 截图云端上传状态的离线判定(只读)
//
// 数据源: userdata\<uid>\760\screenshots.vdf (Steam 客户端维护的元数据库,
//         文本 VDF,VdfParser 可直接解析)
//
// 判定式(docs/offline-cloud-upload-status.md §4.1):
//   已上传 ⇔ publishedfileid 非空(且≠"0") 且 hscreenshot ≠ 哨兵值
//   未上传 ⇔ 无 publishedfileid 且 hscreenshot == 哨兵值
//
// 大整数陷阱(§4.3): hscreenshot 形如 10286326743963383081 > INT64_MAX,
//   必须以字符串原样比较,任何 _wtoi/_i64 转换都会溢出截断。
//
// key 设计: "<uid>|<basename 小写>" —— basename 与磁盘 jpg 比对;
//   多用户时键含 uid,互不干扰。
// ---------------------------------------------------------------------------
enum class CloudState
{
    Unknown     = 0,  // vdf 不存在/解析失败/游戏块缺失
    Orphan      = 1,  // 磁盘有图,vdf 无登记(启动 Steam 后会自动索引)
    NotUploaded = 2,  // 本地存在,尚未上传云端
    Uploaded    = 3,  // 已上传(publishedfileid 有效)
};

const wchar_t* const kHsScreenshotSentinel =
    L"18446744073709551615"; // UINT64_MAX,未上传哨兵值

struct CloudEntry
{
    CloudState State = CloudState::Unknown;
    CString    PublishedFileId; // Workshop ID(仅展示,不做数值运算)
};

class ScreenshotCloudStatus
{
public:
    // 解析单个用户的 screenshots.vdf;失败(不存在/损坏)返回 false,
    // 此时该用户所有截图应视为 Unknown,不得部分采用(§6.6)
    bool LoadForUser(const CString& userId, const CString& vdfPath);

    // 查询状态;fileName 应为小写 basename(如 20260313040754_1.jpg)
    // 找不到条目返回 nullptr(调用方结合 vdf 是否加载成功判 Orphan/Unknown)
    const CloudEntry* Query(const CString& userId, const CString& fileNameLower) const;

private:
    std::unordered_map<std::wstring, CloudEntry> m_entries;

    static CString MakeKey(const CString& userId, const CString& fileNameLower);
    static CString BasenameLower(const CString& vdfFileName); // 正斜杠路径→末段→小写
};
