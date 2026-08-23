#include "I18n.h"

// ---------------------------------------------------------------------------
// 中英字符串表(顺序与 StrId 枚举严格一致)
// 带占位符的条目,中英文的 %d/%s 个数与类型必须相同
// ---------------------------------------------------------------------------
namespace
{
    struct Pair { const wchar_t* zh; const wchar_t* en; };

    const Pair kTable[] = {
        /* S_TITLE        */ { L"Steam 截图管理器",              L"Steam Screenshot Manager" },
        /* S_STATS        */ { L"截图  ·  %d 款游戏  ·  %d 张截图", L"Screenshots  ·  %d games  ·  %d shots" },
        /* S_NO_STEAM     */ { L"未找到 Steam 截图 — 请确认已安装 Steam 并有截图记录",
                               L"No Steam screenshots found — make sure Steam is installed and has screenshots" },
        /* S_IMPORT_BTN   */ { L"导入…",                        L"Import…" },
        /* S_SELECT_FIRST */ { L"请先在左侧选择一个游戏。",       L"Select a game on the left first." },
        /* S_TIP          */ { L"导入",                         L"Import" },
        /* S_GRID_EMPTY   */ { L"在左侧选择一个游戏以浏览截图",   L"Select a game on the left to browse screenshots" },
        /* S_SHOTS_FMT    */ { L"%u  ·  %d 张截图",             L"%u  ·  %d screenshots" },
        /* S_SHOTS_ONLY   */ { L"%d 张截图",                    L"%d screenshots" },
        /* S_ALL_CLOUD    */ { L"  ·  \u2601全部",              L"  ·  all \u2601" },
        /* S_CTX_OPEN     */ { L"在资源管理器中打开",            L"Open in Explorer" },
        /* S_CTX_DELETE   */ { L"删除此截图及缩略图",            L"Delete screenshot && thumbnail" }, // && 转义菜单加速键前缀
        /* S_DEL_CONFIRM  */ { L"确定删除这张截图吗?\n\n%s\n\n将同时删除其缩略图,且不可恢复。",
                               L"Delete this screenshot?\n\n%s\n\nIts thumbnail will also be removed. This cannot be undone." },
        /* S_DEL_TITLE    */ { L"删除截图",                     L"Delete Screenshot" },
        /* S_PREVIEW_CAP  */ { L"预览",                         L"Preview" },
        /* S_PREVIEW_FAIL */ { L"无法加载: %s",                  L"Failed to load: %s" },
        /* S_LOADING      */ { L"加载中…",                      L"Loading…" },
        /* S_CLOUD_UPLOADED*/{ L"   ·   \u2601 已上传 (pid %s)",  L"   ·   \u2601 uploaded (pid %s)" },
        /* S_CLOUD_UPLOADED_NOID */ { L"   ·   \u2601 已上传",    L"   ·   \u2601 uploaded" },
        /* S_IMP_TITLE    */ { L"导入截图 — %s",                L"Import Screenshots — %s" },
        /* S_IMP_HINT     */ { L"把图片拖入下方列表,或点击 [选取图片]。双击文件名可修改日期。",
                               L"Drag images into the list below, or click [Pick Files]. Double-click a file name to edit its date." },
        /* S_PICK_FILES   */ { L"选取图片…",                    L"Pick Files…" },
        /* S_DONE         */ { L"完成",                         L"Done" },
        /* S_CANCEL       */ { L"取消",                         L"Cancel" },
        /* S_PROCESSING   */ { L"处理中…",                      L"Processing…" },
        /* S_ROW_FAILED   */ { L"失败",                         L"Failed" },
        /* S_NOTHING      */ { L"没有可导入的图片。",            L"No images to import." },
        /* S_CONFIRM_MSG  */ { L"将把 %d 张截图导入到:\n\n%s\n\n(%s)\n\n确定继续吗?",
                               L"Import %d screenshots into:\n\n%s\n\n(%s)\n\nContinue?" },
        /* S_CONFIRM_CAP  */ { L"确认导入",                     L"Confirm Import" },
        /* S_RESULT_FMT   */ { L"成功 %d 张,失败 %d 张。",       L"%d imported, %d failed." },
        /* S_RESULT_CAP   */ { L"导入结果",                     L"Import Result" },
        /* S_IMG_FILTER   */ { L"图片文件|*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp|所有文件|*.*||",
                               L"Images|*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp|All files|*.*||" },
        /* S_DATE_TITLE   */ { L"修改日期",                     L"Edit Date" },
        /* S_BTN_OK       */ { L"确定",                         L"OK" },
        /* S_EXPORT_BTN   */ { L"导出…",                        L"Export…" },
        /* S_EXPORT_TITLE */ { L"导出 — %s",                    L"Export — %s" },
        /* S_EXPORT_CAP   */ { L"导出",                         L"Export" },
        /* S_NO_FFMPEG    */ { L"未检测到 ffmpeg。\n\n请下载 ffmpeg 并将其所在目录加入 PATH 环境变量后重试。\n\n下载地址: https://www.gyan.dev/ffmpeg/builds/",
                               L"ffmpeg was not found.\n\nPlease download ffmpeg and add its folder to the PATH environment variable, then try again.\n\nDownload: https://www.gyan.dev/ffmpeg/builds/" },
        /* S_OPEN_DL_PAGE */ { L"打开下载页",                   L"Open download page" },
        /* S_NO_ENCODER   */ { L"当前 ffmpeg 缺少 AV1 编码器(需要 libsvtav1 或 libaom-av1)。\n\n请更换完整版 ffmpeg 构建(gyan.dev full 版即可)后重试。",
                               L"The ffmpeg on this machine lacks an AV1 encoder (libsvtav1 or libaom-av1 required).\n\nPlease switch to a full ffmpeg build (e.g. gyan.dev full build) and try again." },
        /* S_PICK_DIR     */ { L"选择导出目录",                  L"Choose export folder" },
        /* S_EXPORT_STATUS*/ { L"导出中  %d / %d",               L"Exporting  %d / %d" },
        /* S_EXPORT_CURRENT*/{ L"当前: %s",                      L"Current: %s" },
        /* S_EXPORT_DONE  */ { L"导出完成: 成功 %d,失败 %d",     L"Export finished: %d succeeded, %d failed" },
        /* S_EXPORT_CANCELLED*/{ L"已取消: 完成 %d,失败 %d",      L"Cancelled: %d done, %d failed" },
        /* S_STOP_BTN     */ { L"取消导出",                     L"Cancel export" },
        /* S_FAILS_HEAD   */ { L"失败(%d):",                    L"Failed (%d):" },
        /* S_EXPORT_DIR_FAIL*/{ L"无法创建输出目录:\n%s",         L"Cannot create output folder:\n%s" },
    };

    constexpr int kCount = sizeof(kTable) / sizeof(kTable[0]);

    // 当前语言(Init 时确定)
    Lang g_lang = LANG_ZH;

    // 按系统语言探测默认值:中文(zh-*) → 中文
    Lang DetectSystemLang()
    {
        WORD ui = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(ui) == LANG_CHINESE)
            return LANG_ZH;
        return LANG_EN;
    }

    // 注册表读写(HKCU\Software\steamshot-mgr)
    bool LoadPref(Lang& out)
    {
        HKEY hKey = nullptr;
        if (::RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\steamshot-mgr", 0,
                           KEY_READ, &hKey) != ERROR_SUCCESS)
            return false;
        wchar_t buf[8]{};
        DWORD size = sizeof(buf), type = 0;
        LSTATUS st = ::RegQueryValueEx(hKey, L"Language", nullptr, &type,
                                       reinterpret_cast<LPBYTE>(buf), &size);
        ::RegCloseKey(hKey);
        if (st != ERROR_SUCCESS || type != REG_SZ)
            return false;
        if (wcscmp(buf, L"zh") == 0)      { out = LANG_ZH; return true; }
        if (wcscmp(buf, L"en") == 0)      { out = LANG_EN; return true; }
        return false;
    }

    void SavePref(Lang l)
    {
        HKEY hKey = nullptr;
        if (::RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\steamshot-mgr", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            return;
        const wchar_t* v = (l == LANG_ZH) ? L"zh" : L"en";
        ::RegSetValueEx(hKey, L"Language", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(v),
                        static_cast<DWORD>((wcslen(v) + 1) * sizeof(wchar_t)));
        ::RegCloseKey(hKey);
    }
}

namespace I18n
{

void Init()
{
    if (!LoadPref(g_lang))          // 用户没手动切过 → 跟随系统语言
        g_lang = DetectSystemLang();
}

Lang Cur() { return g_lang; }

void SetCur(Lang l)
{
    g_lang = l;
    SavePref(l);
}

void Toggle()
{
    SetCur(g_lang == LANG_ZH ? LANG_EN : LANG_ZH);
}

CString T(int id)
{
    if (id < 0 || id >= kCount)
        return CString();
    return CString(g_lang == LANG_ZH ? kTable[id].zh : kTable[id].en);
}

CString LangName()
{
    return g_lang == LANG_ZH ? CString(L"中文") : CString(L"English");
}

} // namespace I18n
