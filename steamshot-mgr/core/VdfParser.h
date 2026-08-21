#pragma once

#include <afx.h>
#include <atlstr.h>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// VdfNode —— Valve 文本格式 KeyValues(.vdf / .acf)的简易解析结果节点
// 仅实现"只读"需求:解析 key-value 对与嵌套区块,不支持序列化写回。
// ---------------------------------------------------------------------------
class VdfNode
{
public:
    CString                             Key;        // 节点名(大小写不敏感,原样保留)
    CString                             Value;      // 叶子节点的值;区块节点为空
    std::vector<std::unique_ptr<VdfNode>> Children; // 子节点

    // 按 key 查找子节点(不区分大小写),找不到返回 nullptr
    const VdfNode* Find(LPCTSTR key) const;

    // 取子节点的字符串值;不存在时返回 fallback
    CString GetString(LPCTSTR key, LPCTSTR fallback = L"") const;
};

// 解析整个 vdf/acf 文本文件,返回根节点;失败返回 nullptr
std::unique_ptr<VdfNode> ParseVdfFile(LPCTSTR filePath);
