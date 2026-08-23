#pragma once

#include <afx.h>
#include <atlstr.h>
#include <atlcomtime.h>
#include <string>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Exporter —— AVIF 批量导出的纯逻辑工具集(只读 Steam 目录;只写用户选的目标目录)
//
// 依赖外部 ffmpeg(用户自行安装并加入 PATH):
//   * LocateFfmpeg: SearchPath 扫 PATH 定位 ffmpeg.exe
//   * ProbeEncoder: 解析 `ffmpeg -encoders`,优先 libsvtav1(快),回退 libaom-av1
//
// 单张导出命令:
//   ffmpeg -y -hide_banner -loglevel error -i 输入.jpg
//          -c:v <编码器> -still-picture 1 -crf 25 输出.avif
//   CRF 25 = 压缩率 60% 的映射(63 × (1-0.60) ≈ 25,AV1 CRF 越小质量越高)
//
// 导出文件名: YYYY-MM-DD-HH-MM-SS.avif(取自截图文件名时间戳)
//   同秒冲突自动顺延 1 秒,绝不覆盖;完成后 SetFileTime 设为文件名时间
// ---------------------------------------------------------------------------
class Exporter
{
public:
    // ---- ffmpeg 定位与能力探测 ----

    // SearchPath 扫 PATH 定位 ffmpeg.exe,找到返回 true 且 ffmpegPath = 完整路径
    static bool LocateFfmpeg(CString& ffmpegPath);

    // 运行 `ffmpeg -hide_banner -encoders`,优先 libsvtav1,回退 libaom-av1
    // 都没有返回空串(提示用户更换完整版 ffmpeg 构建)
    static CString ProbeEncoder(LPCTSTR ffmpegPath);

    // ---- 目录选择(系统 Vista+ 文件夹对话框) ----
    static bool PickFolder(HWND parent, CString& outDir);

    // ---- 名称处理 ----

    // NTFS 非法字符 \ / : * ? " < > | 转义为对应全角,半角空格保留
    static CString SanitizeName(LPCTSTR name);

    // 生成唯一导出文件名 YYYY-MM-DD-HH-MM-SS.avif
    // 同秒冲突(磁盘已有或本批次已用)自动顺延 1 秒;finalTime 返回顺延后的时间
    static CString MakeUniqueName(const COleDateTime& t, LPCTSTR dstDir,
                                  std::unordered_set<std::wstring>& used,
                                  COleDateTime& finalTime);

    // ---- 执行 ----

    // 运行 ffmpeg 导出单张;成功(exit code 0 且目标存在)返回 true
    // outProcess 非空时返回进程句柄(调用方负责 CloseHandle),用于取消时终止;
    // 此时函数不等待,调用方自行 WaitForMultipleObjects
    static bool RunFfmpeg(LPCTSTR ffmpegPath, LPCTSTR encoder,
                          LPCTSTR srcJpg, LPCTSTR dstAvif,
                          HANDLE* outProcess = nullptr);

    // 把文件的创建/修改/访问时间设为 t(本地时间)
    static bool SetFileTimes(LPCTSTR path, const COleDateTime& t);

    // AV1 质量参数:压缩率 60% → CRF = 63 × (1 - 0.60) ≈ 25
    static constexpr int kCrf = 25;
};
