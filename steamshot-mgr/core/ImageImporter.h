#pragma once

#include <afx.h>
#include <atlstr.h>
#include <atlimage.h>
#include <vector>

// ---------------------------------------------------------------------------
// ImageImporter —— 图片导入的格式转换与压缩(纯逻辑,不做 UI)
//
// 处理流程(Analyze):
//   1. 用 CImage 读入任意 GDI+ 支持的格式(jpg/png/bmp/gif/tif…)
//   2. 记录原图大小;>5MB 记警告①
//   3. 统一转为 JPEG 编码:
//        - 先降质量 90→80→…→10,仍 >1MB 则
//        - 等比缩小分辨率 ×0.85 迭代,质量重置 90 再降
//        - 直至 ≤1MB 或到下限(分辨率 <320px 且质量已到 10)
//   4. 仍 >1MB 记警告②
//   5. 结果先写入 tmp 工作目录(防内存爆炸),确认导入时才搬到 screenshots\
//
// 全部输出为磁盘文件路径;源文件只读,不改动。
// ---------------------------------------------------------------------------

// 警告标志(可组合)
enum ImportWarn : unsigned
{
    WarnNone        = 0,
    WarnLargeSrc    = 1 << 0,  // ① 原图 >5MB(已尝试压缩)
    WarnStillBig    = 1 << 1,  // ② 降到下限仍 >1MB
    WarnUnsupported = 1 << 2,  // 无法解码/不支持的格式
};

struct ImportResult
{
    CString   SrcPath;      // 原始文件
    CString   TmpPath;      // 转换后落在 tmp 的 jpg 路径(成功时非空)
    long long SrcSize  = 0; // 原图字节
    long long OutSize  = 0; // 输出字节
    int       SrcW = 0, SrcH = 0;   // 原图分辨率
    int       OutW = 0, OutH = 0;   // 输出分辨率
    unsigned  Warn = WarnNone;
    CString   Error;        // 失败原因(成功为空)
};

class ImageImporter
{
public:
    // 目标阈值
    static constexpr long long kTargetBytes = 1LL * 1024 * 1024;      // 1MB
    static constexpr long long kLargeSrcBytes = 5LL * 1024 * 1024;    // 5MB

    // 分析并转换单张图片,结果写入 tmpDir 下的唯一临时文件
    // tmpDir 须已存在;成功返回 true 且 result.TmpPath 有效
    bool Analyze(LPCTSTR srcPath, LPCTSTR tmpDir, ImportResult& result);

    // 生成缩略图(宽 ~200px 等比),写入 outThumbPath;失败返回 false
    static bool MakeThumbnail(LPCTSTR srcJpg, LPCTSTR outThumbPath, int maxW = 200);

    // 取文件字节大小
    static long long FileSize(LPCTSTR path);

private:
    // 用 GDI+ 将 image 以指定 JPEG 质量编码保存到 outPath
    static bool SaveJpeg(CImage& image, LPCTSTR outPath, int quality);

    // 取得 JPEG 编码器 CLSID
    static bool GetJpegEncoderClsid(CLSID& clsid);
};
