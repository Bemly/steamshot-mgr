# steamshot-mgr 项目约束与经验(opencode 自动加载)

纯 MFC(Win11 x64,无第三方 UI 库)的 Steam 截图管理器。需求与功能详见 README.md / README.en.md,技术方案文档在 docs/。

## 构建环境

- VS2026 Community:`C:\Program Files\Microsoft Visual Studio\18\Community`
- MSBuild 构建(验证时 grep `error C|error LNK|fatal`):

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" steamshot-mgr.sln -t:Build -p:Configuration=Debug -p:Platform=x64 -v:m -nologo
```

- PlatformToolset **v145**,Windows SDK **10.0.26100.0**,仅 x64,MFC 共享 DLL,**`/utf-8`**(源码为 UTF-8,缺该选项 GBK 代码页下中文注释报 C4819)
- MSBuild 的 `-p:` 属性优先级高于 vcxproj 硬编码,CI 可覆盖 toolset/SDK 版本

## Git / 发布流程(每次会话必做)

1. 小步提交,**中文** commit message;仓库已配 GPG 签名(commit.gpgsign=true,签名密钥指纹 7DCE86D5DB14B5E2411BF6B1A9020BC99C68A6ED,gpg.program 指向 Git 自带 gpg.exe),**勿改写或绕过签名配置**
2. push 到 origin main(GitHub: Bemly/steamshot-mgr,private)
3. 构建 Release|x64(**不打 zip**,用户明确不要)
4. 触发发布 workflow:

```bash
GH="C:/Program Files/GitHub CLI/gh.exe"   # bash 无 PATH,必须全路径
"$GH" workflow run release.yml --ref main
"$GH" run watch <run_id> --exit-status      # 确认成功
```

release tag 形如 `v26.08.23`(同日多次自动加序号),由 CI 构建并附 exe。

## 架构约定

- `core/`:数据层,全部只读解析
  - SteamLocator:注册表 SteamPath + libraryfolders.vdf 收集库目录
  - VdfParser:文本 KeyValues(acf/vdf);AppInfoDb:appinfo.vdf 二进制 v41(magic 0x07564429,键=字符串表 u32 索引)
  - GameCatalog:**三级链路** manifest → appinfo.vdf(wanted 过滤)→ shortcutnames,回退 `App <id>`
  - ScreenshotStore:扫描 + 文件名时间戳解析 + 时间降序;ImageImporter:转 JPEG 压缩;ShotNameGen:`YYYYMMDDHHMMSS_1.jpg` 同秒冲突顺延 1 秒
- `ui/`:自绘控件,统一配色走 Theme.*(Steam 色 #171A21/#1B2838/#66C0F4/#C7D5E0)
- **读写策略**:浏览只读(GENERIC_READ/KEY_READ);导入、删除是显式二次确认后的写操作;导入转换落盘 `<Steam>\userdata\<uid>\760\remote\tmp\`,取消时清理 tmp 残留
- **i18n**:所有 UI 文案必须走 `I18n::T(StrId)`;新增文案要在 `I18n.h` 的 StrId 枚举和 `I18n.cpp` 的 kTable **同步添加且顺序严格一致**;默认语言跟随系统(PRIMARYLANGID==LANG_CHINESE→中文),手动选择存 HKCU\Software\steamshot-mgr\Language;**游戏名是数据不翻译**
- README 中英双版(README.md / README.en.md),重大功能更新需两版同步;技术方案文档放 docs/(面向后续实现的参考文档)

## 踩过的坑(重要,勿重蹈)

1. **MFC 子控件的鼠标键盘消息不会发给父对话框**:列表双击必须用 `ON_LBN_DBLCLK(IDC_IMP_LIST, ...)` 通知;且 OnLButtonDown 里 SetCapture 会吃掉双击序列(拖拽排序因此移除)
2. **模态子对话框必须重写 OnOK()**:默认 OnOK 只 EndDialog 不读控件值——"确定后数据没变"十有八九是这个(日期编辑对话框踩过)
3. **CImage::Save 不支持 JPEG 质量参数**:要用 GDI+ `Bitmap::Save(HBITMAP)`+EncoderParameters(EncoderQuality);GDI+ 需在 App InitInstance 显式 GdiplusStartup(已做,勿删)
4. **头文件位置易错**:CDateTimeCtrl→afxdtctl.h(不在 afxcmn.h!);CMemDC→afxglobals.h;CFileDialog→afxdlgs.h;COleDateTime→atlcomtime.h;std::string/wstring→自己 include <string>
5. **Windows 宏冲突**:枚举名避开 S_OK(= ((HRESULT)0L))等;`min<T>(a,b)` 写法会被 min/max 宏干扰,用三目运算替代
6. **DTP 日期时间控件**:SetFormat(L"yyyy-MM-dd HH:mm:ss") 即可同编辑日期时间;DTS_TIMEFORMAT 样式反而只剩时间
7. **自动化 UI 验证**:PowerShell 抓窗口内容用 PrintWindow(hwnd,hdc,2)(PW_RENDERFULLCONTENT),SetForegroundWindow 经常失败导致 CopyFromScreen 截到桌面
8. GetFileStatus().m_mtime 是 CTime,赋给 COleDateTime 需 COleDateTime(t.GetTime())转换

## 本机 Steam 数据(测试参考)

- 主程序 `C:\Program Files (x86)\Steam`;库:C:\Program Files (x86)\Steam、D:\SteamLibrary、E:\SteamLibrary
- 截图:`userdata\<uid>\760\remote\<AppID>\screenshots\`(文件名即时间戳);缩略图在其 thumbnails\ 下同名文件
- 有效用户 395915010(77 款游戏、3300+ 张截图),可直接用作功能测试数据
