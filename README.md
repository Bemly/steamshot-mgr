# steamshot-mgr —— Steam 截图管理器(纯 MFC · 只读)

一个用**纯 MFC**编写的 Steam 截图浏览器,界面风格仿照 Steam 客户端自带的截图管理器(暗色主题)。自动识别本机 Steam 截图路径,按游戏分组浏览,按时间排序展示。**全程只读,不修改 Steam 目录内任何文件。**

## 原规划(需求)

1. 检查 Win11 的 MFC 开发环境(VS2026 + MFC 组件)。
2. 用纯正 MFC 编写 UI 项目,风格类似 Steam 截图管理器。
3. 自动识别 Steam 截图路径(`userdata\<用户ID>\760\remote\<AppID>\screenshots\`)。
4. **只读不写**:仅读取截图目录,不做任何修改。
5. 预览各游戏编号(AppID,如 763 等)文件夹下的截图。
6. 把文件名(`YYYYMMDDHHMMSS_N.jpg`)作为时间戳,按时间排序并在 UI 上标注。

### 用户确认的技术选型

- 图像加载:**CImage**(GDI+,MFC 自带)
- 项目名称:`steamshot-mgr`,独立文件夹
- 游戏名来源:**appmanifest** 获取中文/英文名称,查不到回退 `App <AppID>`
- 排序:**按时间,最新在前**(默认)
- 缩略图:**直接用 Steam 自己的 `thumbnails\` 缩略图**
- 版本控制:创建 git,每次会话提交,注释与交流用中文

## 完成情况总结

### 环境检查结果(Win11)

| 项目 | 状态 |
|------|------|
| Visual Studio Community 2026 (18.9.1) | 已安装 |
| MFC/ATL 组件(工具集 14.51.36231,`_MFC_VER 0x0E00`) | 已安装,x86+x64 库齐全 |
| 旧版工具集 v141(14.16,兼容 VS2017) | 已安装,含 Spectre 库 |
| Windows SDK 10.0.26100 | 已安装 |
| PlatformToolset | **v145**(项目已配置) |

结论:MFC 为 VS2026 自带最新版,环境完整可用。

### 已实现功能

- **自动定位 Steam**:读注册表 `HKCU\Software\Valve\Steam\SteamPath`,解析 `libraryfolders.vdf` 收集所有库目录(主库 + D 盘等附加库)。
- **扫描截图**:遍历所有用户的 `760\remote\<AppID>\screenshots\`,识别 75 款游戏、3014 张截图(实测数据)。
- **时间戳排序**:从文件名解析时间,按时间**降序(最新在前)**排列,缩略图下方标注 `YYYY-MM-DD HH:MM:SS`。
- **游戏名映射**:从 `appmanifest_*.acf` 的 `name` 字段读取(支持中文,如"小黑盒加速器"),未安装游戏回退显示 `App <AppID>`。
- **Steam 风格 UI**:暗色主题(背景 `#171A21`、面板 `#1B2838`、强调蓝 `#66C0F4`),左侧游戏列表 + 右侧缩略图网格 + 顶部统计栏。
- **虚拟化网格**:不一次性加载 3000+ 张图,仅解码可视区,后台线程池解码,LRU 内存缓存(上限 300 张),切换游戏自动作废旧任务。
- **缩略图策略**:优先加载 Steam 自带 `thumbnails\` 缩略图;缺失或损坏时回退解码原图。
- **全尺寸预览**:双击缩略图打开,等比缩放适配窗口,`←`/`→` 或 `A`/`D` 或点击左右半区切换,`Esc` 关闭,底部显示时间戳、文件名、分辨率、序号。
- **刷新**:`Ctrl+R` 重新扫描。

### 只读保障

- 文件仅以 `GENERIC_READ` 打开,注册表仅以 `KEY_READ` 读取。
- 缩略图缓存全部驻留内存,不写临时文件,不触碰 Steam 目录。

## 项目结构

```
steamshot-mgr/
├─ steamshot-mgr.sln
└─ steamshot-mgr/
   ├─ steamshot-mgr.vcxproj     # VS2026, v145, x64, MFC 共享 DLL, /utf-8
   ├─ steamshot-mgr.cpp/.h      # CWinApp 入口
   ├─ MainFrame.cpp/.h          # 主窗口(左列表 + 右网格 + 顶栏)
   ├─ steamshot-mgr.rc          # 资源(图标/对话框/加速键/版本)
   ├─ core/
   │  ├─ SteamLocator.*         # 注册表 + libraryfolders.vdf 定位库目录
   │  ├─ VdfParser.*            # 文本 KeyValues 解析器(只读)
   │  ├─ GameCatalog.*          # AppID→游戏名(appmanifest),回退 App <id>
   │  └─ ScreenshotStore.*      # 扫描截图、文件名时间戳解析、降序排序
   └─ ui/
      ├─ Theme.*                # Steam 暗色配色与字体
      ├─ GameListCtrl.*         # 左侧自绘游戏列表
      ├─ ThumbGridView.*        # 虚拟化缩略图网格(后台解码 + LRU 缓存)
      └─ PreviewDlg.*           # 全尺寸预览(键盘/点击导航)
```

## 编译与运行

1. 用 **Visual Studio 2026** 打开 `steamshot-mgr.sln`。
2. 选择 `Debug | x64` 或 `Release | x64`,生成。
3. 输出:`x64\<配置>\steamshot-mgr.exe`,双击即用(依赖 MFC 共享 DLL,VS2026 运行库已随系统安装)。

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
| 主界面 `Ctrl+R` | 重新扫描截图 |
| 调整窗口大小 | 网格列数自适应 |
