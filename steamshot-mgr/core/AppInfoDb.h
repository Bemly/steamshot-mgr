#pragma once

#include <afx.h>
#include <atlstr.h>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// AppInfoDb —— Steam appcache/appinfo.vdf 离线解析(只读)
//
// appinfo.vdf 为二进制格式(v41, magic 0x07564429):
//   头 16 字节: magic u32 | universe u32 | string_table_offset u64
//   字符串表:   u32 数量 + N 个 null 结尾 UTF-8 字符串
//   App 条目:   appid u32 (0=结束) + size u32 + 固定头 + 二进制 VDF blob
//   v41 的二进制 VDF 键为"字符串表索引"(u32),非明文。
//
// 参考: docs/offline-appid-mapping.md 第 4 节(含逐字段偏移与实测数据)。
//
// 用法: 只提取截图目录涉及的 AppID(wanted 集合),单遍扫描零额外开销;
//       全量扫描 6 MiB 约 1-2 秒,wanted 过滤后远快于此。
// ---------------------------------------------------------------------------
class AppInfoDb
{
public:
    // 解析 appinfo.vdf,把 wanted 中命中的 AppID→name 写入 out
    // 返回成功提取名称的数量;文件不存在/格式不符返回 -1 或 0
    int LoadWanted(const std::unordered_set<unsigned int>& wanted,
                   std::unordered_map<unsigned int, CString>& out);

private:
    // 在单个条目 blob 中定位 common.name 字符串(键为 name_idx 的 String 节点)
    // 返回是否找到
    static bool ExtractName(const BYTE* blob, size_t len, unsigned int nameIdx, CStringW& out);
};
