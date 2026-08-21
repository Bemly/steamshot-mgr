#pragma once

#include <afx.h>
#include <atlstr.h>
#include <atlcomtime.h>
#include <string>
#include <unordered_set>

// ---------------------------------------------------------------------------
// ShotNameGen —— Steam 截图标准文件名生成器
//
// 规则: YYYYMMDDHHMMSS_N.jpg(N 恒为 1)
//   * 基础时间取图片的"修改日期"
//   * 若目标目录(或本批次)已存在同名,则秒数 +1 继续试,直到唯一
//     (用户决策:同秒冲突时往后延 1 秒,而非递增 _N)
// ---------------------------------------------------------------------------
class ShotNameGen
{
public:
    // targetDir: 该游戏 screenshots\ 目录(用于检测与已有文件冲突)
    explicit ShotNameGen(LPCTSTR targetDir);

    // 由修改时间生成唯一文件名(不含路径),如 20260313162413_1.jpg
    // mtime: 文件修改时间(本地时间)
    CString Generate(const COleDateTime& mtime);

    // 用户改完日期后,以新时间重新生成并避让(原占用名释放)
    // oldName: 该条目当前占用的名字;返回新的唯一名
    CString Regenerate(const COleDateTime& newTime, const CString& oldName);

private:
    CString m_targetDir;
    std::unordered_set<std::wstring> m_used; // 本批次已占用的名字(小写)

    static CString Format(const COleDateTime& t);        // 时间→标准文件名
    bool ExistsOnDisk(const CString& name) const;        // 目标目录是否已存在
    bool IsTaken(const CString& name) const;             // 磁盘或批次内是否占用
};
