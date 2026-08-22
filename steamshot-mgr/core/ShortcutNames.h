#pragma once

#include <afx.h>
#include <atlstr.h>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// ShortcutNames —— 非 Steam 游戏名称(只读)
//
// Steam 把非 Steam 游戏(手动添加的快捷方式)的名字登记在:
//   userdata\<UserID>\760\screenshots.vdf   顶层 "shortcutnames" 节
//
// 格式(文本 VDF,VdfParser 可直接解析):
//   "screenshots"
//   {
//       "shortcutnames"
//       {
//           "54321"   "My Local Game"
//           "123456"  "模拟器游戏"
//       }
//       ...
//   }
// ---------------------------------------------------------------------------
class ShortcutNames
{
public:
    // 扫描所有用户的 screenshots.vdf,汇总 shortcutnames 映射
    // 返回成功读取的用户数;out 键为快捷方式 AppID
    int Load(std::unordered_map<unsigned int, CString>& out);

private:
    // 解析单个用户的 screenshots.vdf,把 shortcutnames 并入 out
    void LoadUser(LPCTSTR vdfPath, std::unordered_map<unsigned int, CString>& out);

    // 列出所有用户目录路径(<Steam>\userdata\<uid>)
    static std::vector<CString> EnumUserDirs();
};
