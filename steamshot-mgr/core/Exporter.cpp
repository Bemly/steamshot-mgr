#include "Exporter.h"

#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace
{
    // 启动子进程并可选捕获输出;返回进程句柄与退出码
    // captureOutput 为 true 时把 stdout/stderr 重定向到管道并读入 outText
    bool Launch(LPCTSTR exePath, LPCTSTR params, bool captureOutput,
                HANDLE* outProcess, DWORD& exitCode, std::string* outText)
    {
        exitCode = (DWORD)-1;

        HANDLE hRead = nullptr, hWrite = nullptr;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

        STARTUPINFO si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (captureOutput)
        {
            if (!::CreatePipe(&hRead, &hWrite, &sa, 0))
                return false;
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdOutput = hWrite;
            si.hStdError  = hWrite;
            si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);
        }

        // CreateProcess 需要可修改的命令行缓冲
        std::wstring cmd;
        cmd.reserve(wcslen(exePath) + wcslen(params) + 4);
        cmd += L'"'; cmd += exePath; cmd += L"\" "; cmd += params;

        PROCESS_INFORMATION pi{};
        BOOL ok = ::CreateProcess(nullptr, cmd.data(), nullptr, nullptr,
                                  captureOutput ? TRUE : FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (hWrite) ::CloseHandle(hWrite);

        if (!ok)
        {
            if (hRead) ::CloseHandle(hRead);
            return false;
        }

        // 读取输出(如有)
        if (captureOutput && outText)
        {
            char buf[4096];
            DWORD n = 0;
            while (::ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0)
                outText->append(buf, n);
        }
        if (hRead) ::CloseHandle(hRead);

        if (outProcess)
        {
            ::CloseHandle(pi.hThread);
            *outProcess = pi.hProcess; // 调用方负责等待与关闭
            return true;
        }

        ::WaitForSingleObject(pi.hProcess, INFINITE);
        ::GetExitCodeProcess(pi.hProcess, &exitCode);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return true;
    }

    std::wstring ToLower(const CString& s)
    {
        std::wstring w(s);
        for (auto& c : w)
            c = towlower(c);
        return w;
    }
}

bool Exporter::LocateFfmpeg(CString& ffmpegPath)
{
    // 等价于 where ffmpeg:按 PATH 搜索 ffmpeg.exe
    wchar_t found[MAX_PATH]{};
    if (::SearchPath(nullptr, L"ffmpeg", L".exe", MAX_PATH, found, nullptr) == 0)
        return false;
    ffmpegPath = found;
    return true;
}

CString Exporter::ProbeEncoder(LPCTSTR ffmpegPath)
{
    // 优先 libsvtav1(编码快),回退 libaom-av1(兼容广)
    static const wchar_t* kPrefs[] = { L"libsvtav1", L"libaom-av1" };

    std::string out;
    DWORD ec = 0;
    if (!Launch(ffmpegPath, L"-hide_banner -encoders", true, nullptr, ec, &out))
        return CString();

    // 输出为 ASCII/UTF-8,直接按窄字符查找
    for (auto* pref : kPrefs)
    {
        char narrow[64];
        size_t len = 0;
        wcstombs_s(&len, narrow, pref, _TRUNCATE);
        if (out.find(narrow) != std::string::npos)
            return CString(pref);
    }
    return CString();
}

bool Exporter::PickFolder(HWND parent, CString& outDir)
{
    // Vista+ IFileDialog 文件夹选择;COM 由本函数自管
    HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coOwned = SUCCEEDED(hrInit);

    bool ok = false;
    IFileDialog* fd = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&fd))))
    {
        DWORD opts = 0;
        fd->GetOptions(&opts);
        fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        fd->SetTitle(L"选择导出目录");

        if (SUCCEEDED(fd->Show(parent)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(fd->GetResult(&item)))
            {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
                {
                    outDir = p;
                    ok = true;
                    ::CoTaskMemFree(p);
                }
                item->Release();
            }
        }
        fd->Release();
    }

    if (coOwned)
        ::CoUninitialize();
    return ok;
}

CString Exporter::SanitizeName(LPCTSTR name)
{
    // NTFS 非法字符 → 对应全角(视觉接近且合法)
    CString s(name);
    s.Replace(L'\\', L'＼');
    s.Replace(L'/' , L'／');
    s.Replace(L':' , L'：');
    s.Replace(L'*' , L'＊');
    s.Replace(L'?' , L'？');
    s.Replace(L'"' , L'＂');
    s.Replace(L'<' , L'＜');
    s.Replace(L'>' , L'＞');
    s.Replace(L'|' , L'｜');
    s.Trim();
    if (s.IsEmpty())
        s = L"_";
    return s;
}

CString Exporter::MakeUniqueName(const COleDateTime& t, LPCTSTR dstDir,
                                 std::unordered_set<std::wstring>& used,
                                 COleDateTime& finalTime)
{
    COleDateTime cur = t;
    for (int i = 0; i < 86400; ++i)
    {
        CString name;
        name.Format(L"%04d-%02d-%02d-%02d-%02d-%02d.avif",
                    cur.GetYear(), cur.GetMonth(), cur.GetDay(),
                    cur.GetHour(), cur.GetMinute(), cur.GetSecond());

        if (!used.count(ToLower(name)) &&
            ::PathFileExists((CString(dstDir) + L"\\" + name)) == FALSE)
        {
            used.insert(ToLower(name));
            finalTime = cur;
            return name;
        }
        cur += COleDateTimeSpan(0, 0, 0, 1); // 同秒冲突 → 顺延 1 秒
    }

    // 理论上到不了这里
    finalTime = cur;
    CString fallback;
    fallback.Format(L"%04d-%02d-%02d-%02d-%02d-%02d.avif",
                    cur.GetYear(), cur.GetMonth(), cur.GetDay(),
                    cur.GetHour(), cur.GetMinute(), cur.GetSecond());
    return fallback;
}

bool Exporter::RunFfmpeg(LPCTSTR ffmpegPath, LPCTSTR encoder,
                         LPCTSTR srcJpg, LPCTSTR dstAvif, HANDLE* outProcess)
{
    CString params;
    params.Format(L"-y -hide_banner -loglevel error -i \"%s\" -c:v %s -still-picture 1 -crf %d \"%s\"",
                  static_cast<LPCTSTR>(srcJpg), static_cast<LPCTSTR>(encoder),
                  kCrf, static_cast<LPCTSTR>(dstAvif));

    DWORD ec = (DWORD)-1;
    if (!Launch(ffmpegPath, params, false, outProcess, ec, nullptr))
        return false;

    if (outProcess)
        return true; // 调用方自行等待并校验

    if (ec != 0)
        return false;
    // 目标文件必须真实存在且非空才算成功
    return ::PathFileExists(dstAvif) != FALSE;
}

bool Exporter::SetFileTimes(LPCTSTR path, const COleDateTime& t)
{
    HANDLE h = ::CreateFile(path, FILE_WRITE_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    SYSTEMTIME st;
    t.GetAsSystemTime(st);
    FILETIME local{}, utc{};
    bool ok = ::SystemTimeToFileTime(&st, &local) != FALSE &&
              ::LocalFileTimeToFileTime(&local, &utc) != FALSE &&
              ::SetFileTime(h, &utc, &utc, &utc) != FALSE; // 创建/访问/修改统一为文件名时间
    ::CloseHandle(h);
    return ok;
}
