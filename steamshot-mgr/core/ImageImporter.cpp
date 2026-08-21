#include "ImageImporter.h"

#include <gdiplus.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace
{
    // 生成 tmp 下的唯一临时文件名
    CString MakeTmpName(LPCTSTR tmpDir)
    {
        wchar_t name[MAX_PATH]{};
        ::GetTempFileName(tmpDir, L"imp", 0, name);
        CString p(name);
        // GetTempFileName 生成 .tmp,统一改成 .jpg
        int dot = p.ReverseFind(L'.');
        if (dot > 0)
            p = p.Left(dot);
        p += L".jpg";
        return p;
    }
}

long long ImageImporter::FileSize(LPCTSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!::GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
        return 0;
    LARGE_INTEGER li{};
    li.LowPart  = fad.nFileSizeLow;
    li.HighPart = static_cast<LONG>(fad.nFileSizeHigh);
    return li.QuadPart;
}

bool ImageImporter::GetJpegEncoderClsid(CLSID& clsid)
{
    using namespace Gdiplus;
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0)
        return false;

    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
    if (GetImageEncoders(num, size, info) != Ok)
        return false;

    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(info[i].MimeType, L"image/jpeg") == 0)
        {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

bool ImageImporter::SaveJpeg(CImage& image, LPCTSTR outPath, int quality)
{
    CLSID clsid;
    if (!GetJpegEncoderClsid(clsid))
        return false;

    // CImage::Save 不支持编码参数,改用 GDI+ Bitmap::Save 以控制 JPEG 质量
    Gdiplus::Bitmap bmp(static_cast<HBITMAP>(image.operator HBITMAP()), nullptr);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;

    Gdiplus::EncoderParameters params{};
    ULONG q = static_cast<ULONG>(quality);
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &q;

    ::DeleteFile(outPath);
    return bmp.Save(outPath, &clsid, &params) == Gdiplus::Ok;
}

bool ImageImporter::Analyze(LPCTSTR srcPath, LPCTSTR tmpDir, ImportResult& result)
{
    result = ImportResult{};
    result.SrcPath = srcPath;
    result.SrcSize = FileSize(srcPath);
    if (result.SrcSize <= 0)
    {
        result.Error = L"无法读取文件";
        return false;
    }
    if (result.SrcSize > kLargeSrcBytes)
        result.Warn |= WarnLargeSrc; // ①

    // 解码
    CImage img;
    if (FAILED(img.Load(srcPath)) || img.IsNull())
    {
        result.Warn |= WarnUnsupported;
        result.Error = L"无法解码(不支持的格式)";
        return false;
    }
    result.SrcW = img.GetWidth();
    result.SrcH = img.GetHeight();

    // 目标输出文件
    CString tmpPath = MakeTmpName(tmpDir);

    // work 接管解码后的图,作为迭代编码的工作副本(可能被逐步缩小)
    CImage work;
    work.Attach(img.Detach());

    int quality = 90;
    bool done = false;

    for (int round = 0; round < 40 && !done; ++round) // 40 轮保险
    {
        // 以当前 work 图按 quality 编码
        if (!SaveJpeg(work, tmpPath, quality))
        {
            result.Error = L"JPEG 编码失败";
            return false;
        }
        long long sz = FileSize(tmpPath);
        result.OutSize = sz;

        if (sz <= kTargetBytes)
        {
            done = true;
            break;
        }

        // 超了:先降质量
        if (quality > 10)
        {
            quality = max(quality - 10, 10);
            continue;
        }

        // 质量到下限仍超:缩分辨率 ×0.85,质量重置
        int w = work.GetWidth();
        int h = work.GetHeight();
        if (w < 320 || h < 240)
        {
            // 分辨率也到下限,放弃压缩,保留现状并打警告②
            result.Warn |= WarnStillBig;
            done = true;
            break;
        }

        int nw = static_cast<int>(w * 0.85);
        int nh = static_cast<int>(h * 0.85);

        CImage scaled;
        if (!scaled.Create(nw, nh, 32))
        {
            result.Error = L"内存不足,缩放失败";
            return false;
        }
        HDC dstDC = scaled.GetDC();
        ::SetStretchBltMode(dstDC, HALFTONE);
        work.StretchBlt(dstDC, 0, 0, nw, nh, 0, 0, w, h, SRCCOPY);
        scaled.ReleaseDC();

        work.Destroy();
        work.Attach(scaled.Detach());
        quality = 90; // 重置质量,继续迭代
    }

    result.OutW = work.GetWidth();
    result.OutH = work.GetHeight();
    result.TmpPath = tmpPath;
    return true;
}

bool ImageImporter::MakeThumbnail(LPCTSTR srcJpg, LPCTSTR outThumbPath, int maxW)
{
    CImage img;
    if (FAILED(img.Load(srcJpg)) || img.IsNull())
        return false;

    int w = img.GetWidth();
    int h = img.GetHeight();
    if (w <= maxW)
    {
        // 本来就很小,直接以高质量 jpg 另存
        return SaveJpeg(img, outThumbPath, 90);
    }

    int nh = static_cast<int>(static_cast<double>(h) * maxW / w);
    CImage thumb;
    if (!thumb.Create(maxW, nh, 32))
        return false;
    HDC dstDC = thumb.GetDC();
    ::SetStretchBltMode(dstDC, HALFTONE);
    img.StretchBlt(dstDC, 0, 0, maxW, nh, 0, 0, w, h, SRCCOPY);
    thumb.ReleaseDC();
    return SaveJpeg(thumb, outThumbPath, 85);
}
