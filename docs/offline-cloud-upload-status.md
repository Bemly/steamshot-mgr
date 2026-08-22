# 离线识别截图云端上传状态 功能说明

> 面向其他 Agent 的实现参考文档。目标：**无网络、无 API、无登录凭据**的前提下，判断每张本地 Steam 截图是否已上传到 Steam 云端。本功能全程只读，不写入 Steam 目录。AppID→游戏名的离线映射见 `docs/offline-appid-mapping.md`，本文不重复。

## 1. 概述

判定依据是一个本地文件：`<Steam>\userdata\<SteamID3>\760\screenshots.vdf`。它是 Steam 客户端维护的截图元数据库（文本 VDF），截图上传云端成功后，客户端会把 Workshop `publishedfileid` 与有效句柄写回该文件。读它即可得到"上次 Steam 同步时刻"的上传状态。

现状缺口：`core/ScreenshotStore` 只扫磁盘 `*.jpg`，完全忽略该 vdf；`core/VdfParser` 已能解析文本 VDF，可直接复用，无需新依赖。

```
userdata\<uid>\760\screenshots.vdf   ──VdfParser──▶  map<appid+文件名, UploadState>
                                                        │
ScreenshotStore 扫描 remote\<appid>\screenshots\*.jpg ──┴─▶ 按 basename 小写比对 → 填充状态
```

## 2. 文件定位

```cpp
// Steam 根目录来源同 core/SteamLocator.cpp:4 (HKCU\Software\Valve\Steam → SteamPath)
// 注意：vdf 在 <uid>\760\ 一级，不在 remote\ 内！
<Steam>\userdata\<SteamID3>\760\screenshots.vdf      // 元数据库（本文主角）
<Steam>\userdata\<SteamID3>\760\remote\<AppID>\screenshots\*.jpg   // 图片本体(现有扫描处)
```

* 本机实测（win32）：根 `C:\program files (x86)\steam`，4 个 uid 中仅 1 个存在 `760\screenshots.vdf`；无此文件的 uid 视为全部状态未知。
* 大小实测 1,448,275 字节（约 3300 条记录）；`core/VdfParser.cpp:22` 的 64 MiB 上限足够。

## 3. 文件结构与字段

UTF-8、Tab 缩进（深度=层级）、LF 换行、无注释。结构：

```vdf
"screenshots"
{
\t"<AppID>"                    ← depth1: 游戏块,键=gameid 十进制串
\t{
\t\t"0"                      ← depth2: 序号条目
\t\t{
\t\t\t"type"            "1"
\t\t\t"filename"        "<AppID>/screenshots/20260313040754_1.jpg"   ← 正斜杠!
\t\t\t"thumbnail"       "<AppID>/screenshots/thumbnails/20260313040754_1.jpg"
\t\t\t"imported"        "1"
\t\t\t"timelineid"      "timeline_136573020260312_200728"   ← 新版时间线,可选
\t\t\t"timelinetime"    "26388"
\t\t\t"width"           "1920"
\t\t\t"height"          "1200"
\t\t\t"gameid"          "1365730"
\t\t\t"creation"        "1773346074"                        ← unix 秒
\t\t\t"Permissions"     "8"
\t\t\t"hscreenshot"     "10286326743963383081"              ← u64 句柄
\t\t\t"publishedfileid" "3683725120"                        ← 仅已上传条目才有
\t\t}
\t}
}
```

字段表（本机 79 个游戏块 / 3350 条实测出现率）：

| 字段 | 出现 | 说明 |
|---|---|---|
| type | 100% | 恒为 `"1"`（普通截图） |
| filename | 100% | 相对路径，正斜杠；与磁盘比对的 key = 其 basename |
| thumbnail | 100% | 同上 + `thumbnails/` |
| imported | 100% | 是否经截图管理器导入流程，与上传状态无关 |
| width / height | 100% | 分辨率（可回填 `ScreenshotItem`，免解码原图） |
| gameid | 100% | 普通==AppID；非 Steam 游戏为大数 shortcut id（见 mapping 文档 §5） |
| creation | 100% | unix 时间戳秒（UTC），可与文件名时间戳交叉校验 |
| Permissions | 100% | 位掩码，实测值 {2,4,8,16}，是可见性/隐私标志，**不是**上传判据（§4.2） |
| hscreenshot | 100% | u64 云端句柄；未上传 = 哨兵 `18446744073709551615`(UINT64_MAX) |
| publishedfileid | 80.7% | Workshop/社区 UGC ID，上传成功后由客户端写入；可拼 `steamcommunity.com/sharedfiles/filedetails/?id=<pid>` |
| timelineid / timelinetime | 91.2% | Game Recording 时间线字段，忽略即可 |
| externalfilename | 2.3% | 外部源文件引用（实测 77 条全部已上传） |
| spoiler | 0.7% | 剧透标记 |

顶层还可能存在 `"shortcutnames"` 区（非 Steam 游戏名，mapping 文档已覆盖）；本机当前无此区，解析时按"缺失即跳过"处理。

## 4. 云端状态判定规则（核心）

### 4.1 判定式

对每条 depth2 条目：

```
已上传   ⇔  publishedfileid 存在且非空(防御性再排除 "0")
            且 hscreenshot ≠ "18446744073709551615"
未上传   ⇔  publishedfileid 为空/缺失 且 hscreenshot == 哨兵
未知     ⇔  screenshots.vdf 不存在 / 解析失败 / 该游戏块整体缺失
未登记   ⇔  磁盘有 *.jpg 但 vdf 无对应 basename（孤儿文件）
```

### 4.2 实测验证（为什么不能用 Permissions）

本机 3350 条交叉统计（脚本见 §8）：

| publishedfileid | hscreenshot≠哨兵 | Permissions 分布 | 条数 | 结论 |
|---|---|---|---|---|
| 有 | 是 | 8:1139, 4:1088, 2:474, 16:4 | 2705 | 已上传 |
| 无 | 否(全哨兵) | 2:610, **8:35** | 645 | 未上传 |

* 二分完全干净：2705+645=3350，无一条歧义。
* **Permissions 与上传状态无关**：未上传组里也有 35 条 `Permissions=8`（抽查均为本地文件已删的死记录）。网上流传的"perm 2→8 即已上传"说法在本机不成立，勿采用。
* 哨兵计数自洽：hscreenshot==哨兵恰 645 条 == 无 publishedfileid 的条数。
* 全部已上传条目 `imported=1` 且 thumbnail 字段非空。

### 4.3 大整数陷阱

`hscreenshot` 实测形如 `10286326743963383081` > INT64_MAX，任何 `_wtoi/_wcstoui64/int64` 解析都会溢出或截断。**必须以字符串原样比较**（现有 `VdfNode::Value` 即 CString，天然安全）。同理 `publishedfileid` 只展示、不做数值运算。这也是 JS 系开源工具（node-steam/vdf 等）曾损坏该文件的原因，我们只读不受影响。

## 5. 与磁盘文件的三态交叉

按 basename 小写比对 vdf `filename` 与 `remote\<AppID>\screenshots\*.jpg`：

| 磁盘 | vdf | 状态 | UI 建议 |
|---|---|---|---|
| 有 | 已上传条目 | 已上传 ✓ | 蓝色云徽标 |
| 有 | 未上传条目 | 未上传 ↑ | 橙色徽标 |
| 有 | 无记录 | 未登记(孤儿) | 灰色问号;提示"启动 Steam 后自动索引" |
| 无 | 已上传条目 | 仅云端(本地已删) | 不在网格显示（现有扫描天然排除） |
| 无 | 未上传条目 | 死记录 | 同上，忽略 |

本机实测：磁盘 jpg 3317 张，孤儿 0 张，死记录 33~35 张 —— 死记录真实存在，比对时务必以"磁盘文件为主表左连 vdf"，不要反向遍历 vdf 当主表。

## 6. 实现要点与坑

1. **复用 `ParseVdfFile`**（`core/VdfParser.h:27`）：递归下降解析器对缩进/空白鲁棒；勿学本文分析脚本的逐行正则法（仅适用于严格 Tab 缩进）。
2. 层级导航：`root->Find(L"screenshots")` → 遍历 Children（键=gameid）→ 遍历其 Children（键=序号）→ `GetString(L"filename"/L"publishedfileid"/L"hscreenshot")`。所有 Find 均大小写不敏感，符合预期。
3. 路径归一：vdf 内正斜杠、磁盘反斜杠；只取 basename 转**小写**入 map（`std::unordered_map<CStringA/CString>` 或先 `CString::MakeLower`）。
4. 多用户：map 键需含 uid。建议 `LoadForUser(uid, vdfPath)` 逐用户加载，`Scan()` 在 `ScanUserDir`（`core/ScreenshotStore.cpp:56`）入口处调用一次；同一 AppID 跨用户聚合策略由 UI 决定（推荐"任一已上传即已上传"）。
5. vdf 路径推导：`remoteDir` 形如 `...\<uid>\760\remote`，剥掉末级 `\remote` 拼 `\screenshots.vdf`。
6. Steam 正在运行时可正常读（`ReadFileAsString` 已带 `FILE_SHARE_READ|WRITE`，`core/VdfParser.cpp:16`）；读到半写状态的概率低但存在——解析失败按整用户 Unknown 回退，不要部分采用。
7. 状态时效：反映的是 Steam 上次云同步的结果；他机上上传、本机未同步时会滞后显示"未上传"。离线场景下这是理论极限，无法更准。
8. 保持只读：绝不写回该文件（写会与运行中的 Steam 客户端互踩）。

## 7. 集成方案

新增 `core/ScreenshotCloudStatus.{h,cpp}`：

```cpp
enum class CloudState { Unknown, Orphan, NotUploaded, Uploaded };

class ScreenshotCloudStatus {
    bool LoadForUser(const CString& userId, const CString& vdfPath);
    CloudState Query(const CString& userId, const CString& fileNameLower) const;
private:
    // key: uid + L'|' + basename(lower) → CloudState（或存 pid 供跳转）
};
```

改动点：

* `core/ScreenshotStore.h:13` `ScreenshotItem` 增加 `CloudState Cloud`（默认 Unknown）、可选 `CString PublishedFileId`；`GameShots` 增加 `UploadedCount/NotUploadedCount`。
* `core/ScreenshotStore.cpp:29` 组装 remote 时同步记录 uid；`:87 ScanGameDir` 内 push 前查 `Query()` 填充并累计计数。
* UI（顺序建议）：`ui/ThumbGridView.cpp:169` 卡片右上角画徽标 → `ui/GameListCtrl.cpp:52` 第三行加"已传 X · 未传 Y" → `ui/PreviewDlg` 底部状态串加"已上传/未上传(+pid 链接)" → 最后再加过滤视图（涉及索引映射，单独一步做）。
* 配色进 `ui/Theme`：蓝 `#66C0F4`(已传)、橙 `#E07C24`(未传)、灰(孤儿)。

## 8. 验证

分析脚本（生成 §4.2 数据，可复跑）：`C:\Users\23287\AppData\Local\Temp\opencode\analyze_vdf.py` 与 `analyze_vdf2.py`（Python3，仅 stdlib）。步骤：注册表取根 → 逐行正则切三级结构 → 统计 Permissions×publishedfileid×哨兵 交叉表 → 与磁盘 `os.listdir` 比对孤儿/死记录。

落地后人工验收：

1. 选一款有混合状态的真机游戏（本机例：`4838470` UP5/NO250、`3443820` UP50/NO85），对照 Steam 客户端截图管理器的"VIEW ONLINE / 本地"分组核对徽标一致性。
2. 删一张未上传图的 jpg 后刷新 → 应从网格消失且计数不减 vdf 侧（死记录路径）。
3. 断网启动程序 → 功能应完整可用（纯本地读）。

## 9. 版本

* 编写：2026-08-22 win32 实测（Steam 客户端 2026-08 时点格式，含 timeline 字段）
* 维护：若 Valve 更改字段名/哨兵值，以 §4.1 判定式为准重新抽样校验；`type/vrfilename`（VR 截图）本机未出现，遇异型 type 先跳过再扩展。
