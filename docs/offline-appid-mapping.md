# 离线 AppID → 游戏名映射 功能说明

> 面向其他 Agent 的实现参考文档。覆盖本地可离线解析的全部数据源、文件格式与验证数据。本功能全程只读，不写入 Steam 目录。

## 1. 概述

`steamshot-mgr` 需要在无网络环境下将截图目录名 `<AppID>` 解析为可读游戏名。当前 `core/GameCatalog` 已实现，现扩展为三级离线链路：

```
appmanifest_*.acf (已安装，文本VDF，最高优先级)
  → appcache/appinfo.vdf v41 二进制（含未安装但曾拥有/浏览的游戏，次优先级）
  → screenshots.vdf 顶层 shortcutnames + userdata/.../config/shortcuts.vdf（非 Steam 游戏）
  → 回退 "App <AppID>"
```

实测本机：`manifest 37` + `截图目录 77` 去重后 `offline_appids.csv 100 行` 全部离线可解；`appinfo` 全量可解 `2433` 个。

## 2. 数据源与路径定位

### 2.1 Steam 根目录
```cpp
// core/SteamLocator.cpp:4
RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Valve\\Steam", KEY_READ)
  → SteamPath (注册表内为 '/' 需 Replace('/','\\'))
```
备用：`HKLM\SOFTWARE\Valve\Steam\InstallPath` / 默认 `C:\Program Files (x86)\Steam`。

### 2.2 库目录
文件：`<Steam>\steamapps\libraryfolders.vdf` （文本 VDF）
```vdf
"libraryfolders"
{
  "0" { "path" "C:\\Program Files (x86)\\Steam" ... "apps" { "228980" "..." } }
  "1" { "path" "D:\\SteamLibrary" ... }
  "2" { "path" "E:\\SteamLibrary" ... }
}
```
提取：正则 `"path"\s+"([^"]+)"` → 归一 `\\` → 拼 `\\steamapps`，去重（大小写不敏感）。结果即 `SteamLocator::GetSteamAppsDirs()`。

> 本机实测 3 库：`C:\Steam\steamapps` (2 款) , `D:\SteamLibrary\steamapps` (5 款) , `E:\SteamLibrary\steamapps` (30 款)。

## 3. 文本 VDF 格式（`VdfParser`）

适用于 `libraryfolders.vdf` / `appmanifest_*.acf` / `screenshots.vdf`。

* 编码：UTF-8（无 BOM），`VdfParser.cpp:14 ReadFileAsString` 用 `GENERIC_READ + FILE_SHARE_READ|WRITE` 读入后 `MultiByteToWideChar(CP_UTF8)`。
* 语法：`"key" "value"` 或 `"key" { ... }`，支持 `//` 行注释，转义 `\\` 原样保留。
* 大小限制：单文件 ≤ 64 MiB。
* 解析器：`Tokenizer` 产生 `String / { / } / End`，`ParseBlock` 递归生成 `VdfNode { Key, Value, Children }`，`Find` 大小写不敏感。

### 3.1 appmanifest
路径：`<steamapps>\appmanifest_<AppID>.acf`，文件名即 AppID：
```cpp
// core/GameCatalog.cpp:22
int start = name.Find('_'); int end = name.ReverseFind('.');
_appId = _wtoi(name.Mid(start+1, end-start-1));
```
内容：
```vdf
"AppState"
{
  "appid" "1447430"
  "name"  "小黑盒加速器"   // ← 取此字段
  "installdir" "HeyboxAccelerator"
  ...
}
```
实现：`ParseVdfFile → Find("AppState") → GetString("name") → Trim()`。本机示例：
* `1447430 → 小黑盒加速器` （原生 UTF-8 字节 `e5 b0 8f e9 bb 91...`，`offline_appids.csv` 已验正确）
* `4838470 → My (♂) Life as a Vampire's Maid (♀)` （含 `e2 99 82/80` 符号）

> 注意：`libraryfolders.vdf` 的 `apps` 节仅存 `appid→bytes`，不含名称，切勿用其作名称源。

## 4. 二进制 appinfo.vdf v41 格式（重点）

文件：`<Steam>\appcache\appinfo.vdf`，本机 `6,314,667 字节`。

> 参考：`SteamDatabase/SteamAppInfo`、`ValvePython/steam#462/#464`、`danielknng/steam-appinfo-parser`。本实现按实测 `magic 0x07564429`（v41）编写，兼容 v28/v29 前代。

### 4.1 文件头（16 字节）

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u32 LE | magic | `0x07564429` = `")DV\x07"`（ASCII `29 44 56 07`），v41；旧版 `0x07564428`/`0x07564427` 为 `"(DV\x07"` / `"'DV\x07"` |
| 4 | u32 LE | universe | `1` (Public) |
| 8 | u64 LE | string_table_offset | 字符串表起始偏移。本机 `6,250,763 (0x5f610b)` |

```python
magic = f.read(4)            # b')DV\x07'
universe = struct.unpack('<I', f.read(4))[0]
off = struct.unpack('<Q', f.read(8))[0]
assert f.tell() == 16
```

### 4.2 字符串表（位于 `off`）

```
u32 num_strings                 # 本机 7446
char[] strings[num_strings]     # 依次 null-terminated UTF-8
```
末尾示例：`... 4838472\x00 4838473\x00 ...`。索引 `0..7445` 对应 `appinfo(0) / appid(1) / public_only(2) / common(3) / name(4) / type(5) ...`

```python
f.seek(off)
num = struct.unpack('<I', f.read(4))[0]  # 7446
table = []
for _ in range(num):
    s = b''
    while (c:=f.read(1)) not in (b'\x00', b''):
        s+=c
    table.append(s.decode('utf-8'))
# name_idx = table.index('name') → 4
```

### 4.3 App 条目（偏移 16 至 off 之前，循环直至 appid==0）

每条目（v41）：

| 字段 | 类型 | 大小 |
|---|---|---|
| appid | u32 | 4 |
| size | u32 | 4 | 数据区总长（含后 60 字节固定字段 + 二进制VDF） |
| info_state | u32 | 4 |
| last_updated | u32 | 4 |
| access_token (PICS) | u64 | 8 |
| sha1_text | bytes | 20 |
| change_number | u32 | 4 |
| sha1_binary | bytes | 20 | v40+ 新增（v41 含） |
| binary_vdf | bytes | size-60 |

```python
appid = struct.unpack('<I', f.read(4))[0]
if appid==0: break
size = struct.unpack('<I', f.read(4))[0]
info_state, last = struct.unpack('<II', f.read(8))
token = struct.unpack('<Q', f.read(8))[0]
sha1 = f.read(20); change = struct.unpack('<I', f.read(4))[0]; sha_bin = f.read(20)
blob = f.read(size-60)   # 本机首条 app 5 → blob 25 字节
```

> 旧版（<v40）无 `sha1_binary`，`blob = size - 40`；`<v38` 另含不同头，需按 magic 分支（本文档仅详述 v41，兼容代码见附）。

### 4.4 二进制 VDF（blob，键为索引）

v41 优化：键不再存明文，改为 `u32 索引` 指向上表。

类型字节：

| 字节 | 类型 | 键 | 值 |
|---|---|---|---|
| 0x00 | Dict | u32 idx | 递归子节点，至 0x08 结束 |
| 0x01 | String | u32 idx | null-terminated UTF-8 |
| 0x02 | Int32 | u32 idx | i32 LE |
| 0x03 | Float32 | | f32 |
| 0x05 | WString | | UTF-16LE null-term |
| 0x07 | UInt64 | | u64 |
| 0x0A | Int64 | | i64 |
| 0x08 | End | - | 结束当前 Dict |

**解析示例（真实 dump，app 7）：**
```
00 00 00 00 00                // Dict appinfo(0)
  02 01 00 00 00 07 00 00 00   // Int appid(1)=7
  00 03 00 00 00               // Dict common(3)
    01 04 00 00 00 53 74 65 61 6d 20 43 6c 69 65 6e 74 00  // String name(4)="Steam Client"
    01 05 00 00 00 43 6f 6e 66 69 67 00                  // String type(5)="Config"
    ...
  08                          // End common
08                            // End appinfo
08                            // End blob
```

**提取 `common.name` 最简实现（无需全解析）：**
```python
name_idx = table.index('name')  # 4
needle = b'\x01' + struct.pack('<I', name_idx)
pos = blob.find(needle)
if pos != -1:
    start = pos+5
    end = blob.find(b'\x00', start)
    name = blob[start:end].decode('utf-8')
```

完整递归解析器见 `C:\Users\23287\AppData\Local\Temp\opencode\gen_csv.py`（已验证 2476 条目 → 2433 个 name）。

> 性能：全量扫描 6.1 MiB 约 1–2 秒；若仅需截图涉及的 AppID（本机 77 个），可先收集 `wanted=set(screenshot_dirs)` 再单遍过滤，零额外开销。

## 5. 非 Steam 游戏

* `userdata/<uid>/760/screenshots.vdf` 顶层 `shortcutnames { "123" "My Game" }`（文本 VDF，可用 `VdfParser`）
* `userdata/<uid>/config/shortcuts.vdf` 二进制（`shortcuts` 数组含 `appname` / `appid`），需同类二进制解析，键为明文。

## 6. 合并策略与验证

优先级：`manifest > appinfo > shortcutnames > App <id>`

> 注：开发期曾用 `gen_csv.py` 生成三份验证 CSV（`offline_appids*.csv`）核对解析正确性，
> 功能落地后已从仓库移除（属可再生的临时产物，不入库）。验证结论保留如下：
>
> * 去重合集 100 行（`manifest 37` + 截图目录 77）全部离线可解；`appinfo` 全量可解 `2433` 个。
> * 校验样例：`1447430 → 小黑盒加速器` 为正确 UTF-8（`e5 b0 8f e9 bb 91...`）。

## 7. 集成到 `steamshot-mgr`

建议新增：
* `core/AppInfoDb.{h,cpp}` — `LoadWanted(set<appid>)` 按上节解析 `appinfo.vdf`，产 `map<appid,name>`
* `core/ShortcutNames.{h,cpp}` — 解析 `screenshots.vdf:shortcutnames`
* `core/GameCatalog.cpp:5 Load(set<appid>)` 改为链式查询（传入 `ScreenshotStore` 收集的 `wanted`）
* `ScreenshotStore.cpp:7 Scan()` 先收集 `wanted` 再传给 `GameCatalog`

只读约束：所有文件 `GENERIC_READ + FILE_SHARE_READ|WRITE`，`RegOpenKeyEx(KEY_READ)`，不写回。

## 8. 验证与参考

* 本机 Steam：`3` 库、`37` 个 manifest（缺 `4673900` 为未完成安装）、`77` 个截图目录、`100` 去重可解。
* 参考实现：`C:\Users\23287\AppData\Local\Temp\opencode\gen_csv.py`（Python，含 v41 完整解析与 CSV 生成）
* 文档：`SteamDatabase/SteamAppInfo` README 版本历史表、`ValvePython/vdf` `binary_load` 源码、`SteamKit2/KeyValue.cs` 类型定义。

## 9. 版本

* 编写：2026-08-21 win32 `appinfo.vdf` v41 (`)DV\x07`) 实测
* 维护：若 Steam 升级 magic/头长，按 `off` 字段与类型表扩展即可。

