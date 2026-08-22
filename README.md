[English](README.en.md) | 中文

# steamshot-mgr —— Steam 截图管理器(纯 MFC)

一个用**纯 MFC**(无第三方 UI 库)编写的 Steam 截图管理工具,界面风格仿照 Steam 客户端自带的截图管理器(暗色主题)。支持浏览本机全部 Steam 截图、按游戏分组、按时间排序,并内置**图片导入**(格式转换 / 压缩 / 自动命名)与截图删除。

## 读写策略

| 操作 | 策略 |
|------|------|
| 浏览 / 预览 / 扫描 | **只读**:文件仅以 `GENERIC_READ` 打开,注册表仅 `KEY_READ`,缓存驻内存 |
| 导入 | **显式写**:点 [完成] 并通过**二次确认**后才写入该游戏的 `screenshots\` 与 `thumbnails\` |
| 删除 | **显式写**:右键删除需二次确认,同时删除原图及对应缩略图 |

> 导入的转换过程会使用 `<Steam>\userdata\<用户>\760\remote\tmp\` 作为临时目录落盘,避免大图全驻内存;取消导入或转换失败时会自动清理 tmp 残留。
> 注:程序内导入后立即可见;若想让 Steam 客户端本身也识别新图,可能需要重启 Steam。

## 功能总览

### 浏览

- **自动定位 Steam**:读注册表 `HKCU\Software\Valve\Steam\SteamPath`,解析 `libraryfolders.vdf` 收集所有库目录(主库 + 附加库)。
- **扫描截图**:遍历所有用户的 `760\remote\<AppID>\screenshots\`,按游戏分组统计。
- **时间戳排序**:从文件名(`YYYYMMDDHHMMSS_N.jpg`)解析拍摄时间,**最新在前**,缩略图下方标注 `YYYY-MM-DD HH:MM:SS`。
- **游戏名映射(三级离线链路)**:`appmanifest_*.acf`(已安装) → `appcache/appinfo.vdf` v41 二进制(未安装但曾拥有,按截图 AppID 集合按需解析) → `screenshots.vdf shortcutnames`(非 Steam 游戏),全部未命中回退 `App <AppID>`;游戏列表小字注明 `AppID · N 张截图`。详见 [docs/offline-appid-mapping.md](docs/offline-appid-mapping.md)。
- **虚拟化网格**:不一次性加载数千张图,仅解码可视区;后台线程解码 + LRU 内存缓存(上限 300 张),切换游戏自动作废旧任务。
- **缩略图策略**:优先用 Steam 自带 `thumbnails\` 缩略图,缺失或损坏时回退解码原图。
- **全尺寸预览**:双击打开,等比适配窗口,`←`/`→` 或 `A`/`D` 或点击左右半区切换,`Esc` 关闭;底部显示时间戳、文件名、原图分辨率、序号。
- **右键菜单**:在资源管理器中定位该文件(`explorer /select`)、删除此截图及缩略图(二次确认,删除后自动刷新并保持当前游戏选中)。

### 导入

- 入口:顶栏右侧 **[导入…]** 按钮(需先在左侧选中一个游戏)。
- **添加图片**:从资源管理器直接**拖拽**进列表,或点 [选取图片] 多选;支持 jpg/png/bmp/gif/tif 等 GDI+ 可解码格式。
- **自动转换**(后台线程逐张处理):
  - 非 JPG 统一转为 JPEG;
  - 大于 **1MB** 自动压缩:先降质量(90→10),仍超标再等比降分辨率(×0.85 迭代),直至达标;
  - 原图大于 **5MB** 标记警告 **①**;降到下限仍压不进 1MB 的标记警告 **②**;无法解码标记 ✕;
  - 转换结果先落盘 `760\remote\tmp\`,不占内存。
- **自动命名**:按图片**修改日期**生成 Steam 标准文件名 `YYYYMMDDHHMMSS_1.jpg`;同秒冲突自动**顺延 1 秒**避让,绝不覆盖已有截图。
- **改日期**:双击列表中的文件名,原位弹出日期时间控件(`yyyy-MM-dd HH:mm:ss`),回车提交 / Esc 取消 / 点外部自动提交,文件名随之重新生成并重新避让。
- **完成导入**:点 [完成] 弹二次确认(显示目标游戏与张数),确认后写入 `screenshots\` 并同步生成 `thumbnails\` 缩略图,主界面立即刷新可见。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2026(v145 工具集,MFC 共享 DLL)+ Windows SDK 10.0.26100 及以上
- 运行依赖 MFC 共享 DLL(VS2026 运行库)

本项目开发时的环境检查结论:MFC/ATL 组件随 VS2026 安装即为最新版(`_MFC_VER 0x0E00`),x86+x64 库齐全。

## 项目结构

```
steamshot-mgr/
├─ steamshot-mgr.sln
├─ docs/
│  └─ offline-appid-mapping.md   # 离线 AppID→游戏名映射说明与验证数据
└─ steamshot-mgr/
   ├─ steamshot-mgr.vcxproj      # VS2026, v145, x64, MFC 共享 DLL, /utf-8
   ├─ steamshot-mgr.cpp/.h       # CWinApp 入口(GDI+ 初始化/清理)
   ├─ MainFrame.cpp/.h           # 主窗口(左列表 + 右网格 + 顶栏含导入按钮)
   ├─ steamshot-mgr.rc           # 资源(图标/对话框/加速键/版本)
   ├─ core/
   │  ├─ SteamLocator.*          # 注册表 + libraryfolders.vdf 定位库目录
   │  ├─ VdfParser.*             # 文本 KeyValues 解析器(acf/vdf,只读)
   │  ├─ GameCatalog.*           # 三级链路取游戏名(manifest→appinfo→shortcutnames)
   │  ├─ AppInfoDb.*            # appinfo.vdf 二进制 v41 解析(只提取 wanted)
   │  ├─ ShortcutNames.*        # 非 Steam 游戏名(screenshots.vdf)
   │  ├─ ScreenshotStore.*       # 扫描截图、文件名时间戳解析、降序排序
   │  ├─ ImageImporter.*         # 转 JPEG/压缩(质量→分辨率迭代)/警告判定
   │  └─ ShotNameGen.*           # 标准文件名生成 + 同秒顺延避让
   └─ ui/
      ├─ Theme.*                 # Steam 暗色配色与字体
      ├─ GameListCtrl.*          # 左侧自绘游戏列表
      ├─ ThumbGridView.*         # 虚拟化缩略图网格(后台解码+LRU+右键菜单)
      ├─ PreviewDlg.*            # 全尺寸预览(键盘/点击导航)
      └─ ImportDlg.*             # 导入对话框(拖入/转换/改名/确认导入)
```

## 编译与运行

1. 用 **Visual Studio 2026** 打开 `steamshot-mgr.sln`。
2. 选择 `Debug | x64` 或 `Release | x64`,生成。
3. 输出:`x64\<配置>\steamshot-mgr.exe`,双击即用。

或命令行(MSBuild):

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" steamshot-mgr.sln -p:Configuration=Release -p:Platform=x64
```

## 操作说明

| 操作 | 效果 |
|------|------|
| 左侧点击游戏 | 右侧显示该游戏截图(最新在前) |
| 双击缩略图 | 全尺寸预览 |
| 预览中 `←`/`→` 或 `A`/`D` | 上一张 / 下一张 |
| 预览中点击图片左/右半区 | 上一张 / 下一张 |
| 预览中 `Esc` | 关闭预览 |
| 网格右键缩略图 | 在资源管理器中打开 / 删除此截图及缩略图(二次确认) |
| 顶栏 [导入…] | 打开当前游戏的导入对话框 |
| 导入对话框:拖入图片 / [选取图片] | 加入待导入列表,后台自动转换压缩 |
| 导入对话框:双击文件名 | 编辑日期时间,文件名自动重生成 |
| 导入对话框:[完成] | 二次确认后写入 screenshots\ 与 thumbnails\ |
| 主界面 `Ctrl+R` | 重新扫描截图 |
| 调整窗口大小 | 网格列数自适应 |

## 许可

仅供个人学习交流使用。
