#pragma once

#include <afx.h>
#include <atlstr.h>

// ---------------------------------------------------------------------------
// I18n —— 中英双语支持(极简运行时切换方案)
//
// * 默认语言跟随系统:主语言为中文(zh-*) → 中文,否则英文
// * 手动切换后写入 HKCU\Software\steamshot-mgr,下次启动沿用用户选择
// * 游戏名属于数据,不参与翻译,始终显示原名
// * 用法:T(S_XXX) 取当前语言文本;带参数处 T() 返回格式串再 CString::Format
// ---------------------------------------------------------------------------
enum Lang { LANG_ZH = 0, LANG_EN = 1 };

// 字符串键(与 I18n.cpp 中表顺序一一对应)
enum StrId {
    S_TITLE,          // 主窗口标题
    S_STATS,          // 顶栏统计 "截图 · %d 款游戏 · %d 张截图"
    S_NO_STEAM,       // 未找到 Steam 截图提示
    S_IMPORT_BTN,     // 导入…
    S_SELECT_FIRST,   // 请先在左侧选择一个游戏。
    S_TIP,            // 提示框标题"导入"
    S_GRID_EMPTY,     // 网格空态:在左侧选择一个游戏以浏览截图
    S_SHOTS_FMT,      // 列表小字 "%u · %d 张截图"(有游戏名)
    S_SHOTS_ONLY,     // 列表小字 "%d 张截图"(无游戏名,标题已是 App <id>)
    S_ALL_CLOUD,      // 列表小字后缀:全部☁(整库已上传)
    S_CTX_OPEN,       // 右键菜单:在资源管理器中打开
    S_CTX_DELETE,     // 右键菜单:删除此截图及缩略图(&&转义)
    S_DEL_CONFIRM,    // 删除确认文案(%s = 文件名)
    S_DEL_TITLE,      // 删除确认框标题
    S_PREVIEW_CAP,    // 预览窗口标题
    S_PREVIEW_FAIL,   // 无法加载: %s
    S_LOADING,        // 缩略图占位:加载中…
    S_CLOUD_UPLOADED, // 预览状态栏: ☁ 已上传 (pid %s)
    S_CLOUD_UPLOADED_NOID, // 预览状态栏: ☁ 已上传(无 pid)
    S_IMP_TITLE,      // 导入截图 — %s
    S_IMP_HINT,       // 导入窗口顶部提示
    S_PICK_FILES,     // 选取图片…
    S_DONE,           // 完成
    S_CANCEL,         // 取消
    S_PROCESSING,     // 行内:处理中…
    S_ROW_FAILED,     // 行内:失败
    S_NOTHING,        // 没有可导入的图片。
    S_CONFIRM_MSG,    // 二次确认文案(%d 张 → %s 游戏名 → %s 目录)
    S_CONFIRM_CAP,    // 确认导入标题
    S_RESULT_FMT,     // 成功 %d 张,失败 %d 张。
    S_RESULT_CAP,     // 导入结果标题
    S_IMG_FILTER,     // 文件选择过滤器描述
    S_DATE_TITLE,     // 修改日期对话框标题
    S_BTN_OK,         // 确定(不能叫 S_OK,与 COM 宏冲突)
    S_EXPORT_BTN,     // 导出…
    S_EXPORT_TITLE,   // 导出 — %s
    S_EXPORT_CAP,     // 弹窗标题"导出"
    S_NO_FFMPEG,      // 未检测到 ffmpeg 提示(含下载地址)
    S_OPEN_DL_PAGE,   // 打开下载页
    S_NO_ENCODER,     // ffmpeg 缺少 AV1 编码器提示
    S_PICK_DIR,       // 选择导出目录
    S_EXPORT_STATUS,  // 导出中 %d / %d
    S_EXPORT_CURRENT, // 当前: %s
    S_EXPORT_DONE,    // 导出完成: 成功 %d,失败 %d
    S_EXPORT_CANCELLED, // 已取消: 完成 %d,失败 %d
    S_STOP_BTN,       // 取消导出
    S_FAILS_HEAD,     // 失败(%d):
    S_EXPORT_DIR_FAIL, // 无法创建输出目录: %s
};

namespace I18n
{
    // 启动时调用:读注册表偏好,无记录则按系统语言设定默认值
    void Init();

    Lang Cur();                       // 当前语言
    void SetCur(Lang l);              // 设置并持久化(调用方负责刷新 UI)
    void Toggle();                    // 中英互换并持久化

    // 取当前语言的字符串;id 必须是合法 StrId
    CString T(int id);

    // 语言按钮上显示的"当前语言名"(中文界面显示"中文",英文显示 English)
    CString LangName();
}
