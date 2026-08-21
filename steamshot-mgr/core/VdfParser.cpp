#include "VdfParser.h"

#include <string>

// ---------------------------------------------------------------------------
// 文本 VDF 解析器(Tokenizer + 递归下降)
// 语法:  "key" "value"  或  "key" { ... }
// ---------------------------------------------------------------------------

namespace
{

// 读取整个文件为 UTF-8 并转成宽字符(Steam 的 acf/vdf 均为 UTF-8 编码)
bool ReadFileAsString(LPCTSTR filePath, CStringW& out)
{
    HANDLE hFile = ::CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > 64ll * 1024 * 1024)
    {
        ::CloseHandle(hFile);
        return false;
    }

    std::string utf8;
    utf8.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ::ReadFile(hFile, utf8.data(), static_cast<DWORD>(utf8.size()), &read, nullptr);
    ::CloseHandle(hFile);
    if (!ok || read == 0)
        return false;

    // UTF-8 -> UTF-16
    int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(read), nullptr, 0);
    if (len <= 0)
        return false;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(read),
                          out.GetBuffer(len), len);
    out.ReleaseBuffer(len);
    return true;
}

class Tokenizer
{
public:
    explicit Tokenizer(const wchar_t* text) : m_p(text) {}

    // 读取下一个记号:"字符串"、"{"、"}" 或文件结束(返回 false 表示结束)
    enum class Type { String, OpenBrace, CloseBrace, End };

    Type Next(CStringW& out)
    {
        SkipWhitespaceAndComments();
        if (*m_p == L'\0')
            return Type::End;
        if (*m_p == L'{')
        {
            ++m_p;
            return Type::OpenBrace;
        }
        if (*m_p == L'}')
        {
            ++m_p;
            return Type::CloseBrace;
        }
        if (*m_p == L'"')
            return ReadQuoted(out) ? Type::String : Type::End;

        // 容错:无引号裸字符串,读到空白为止
        const wchar_t* start = m_p;
        while (*m_p && !iswspace(*m_p) && *m_p != L'{' && *m_p != L'}')
            ++m_p;
        out.SetString(start, static_cast<int>(m_p - start));
        return Type::String;
    }

private:
    const wchar_t* m_p;

    void SkipWhitespaceAndComments()
    {
        for (;;)
        {
            while (*m_p && iswspace(*m_p))
                ++m_p;
            // 支持 // 行注释
            if (m_p[0] == L'/' && m_p[1] == L'/')
            {
                while (*m_p && *m_p != L'\n')
                    ++m_p;
                continue;
            }
            break;
        }
    }

    bool ReadQuoted(CStringW& out)
    {
        ++m_p; // 跳过起始引号
        CStringW result;
        while (*m_p)
        {
            wchar_t c = *m_p;
            if (c == L'"')
            {
                ++m_p;
                out = result;
                return true;
            }
            if (c == L'\\' && m_p[1]) // 转义字符,原样保留下一个字符
            {
                ++m_p;
                result += *m_p;
                ++m_p;
                continue;
            }
            result += c;
            ++m_p;
        }
        return false; // 未闭合
    }
};

// 递归解析一个区块的内容,直到遇到 "}"
bool ParseBlock(Tokenizer& tz, VdfNode& node)
{
    CStringW token;
    for (;;)
    {
        Tokenizer::Type t = tz.Next(token);
        if (t == Tokenizer::Type::End || t == Tokenizer::Type::CloseBrace)
            return true;
        if (t != Tokenizer::Type::String)
            return false;

        auto child = std::make_unique<VdfNode>();
        child->Key = token;

        t = tz.Next(token);
        if (t == Tokenizer::Type::String)
        {
            child->Value = token; // key-value 叶子
        }
        else if (t == Tokenizer::Type::OpenBrace)
        {
            if (!ParseBlock(tz, *child)) // 嵌套区块
                return false;
        }
        else
        {
            return false; // 语法错误
        }
        node.Children.push_back(std::move(child));
    }
}

} // namespace

const VdfNode* VdfNode::Find(LPCTSTR key) const
{
    for (const auto& c : Children)
    {
        if (c->Key.CompareNoCase(key) == 0)
            return c.get();
    }
    return nullptr;
}

CString VdfNode::GetString(LPCTSTR key, LPCTSTR fallback) const
{
    const VdfNode* n = Find(key);
    return n ? CString(n->Value) : CString(fallback);
}

std::unique_ptr<VdfNode> ParseVdfFile(LPCTSTR filePath)
{
    CStringW text;
    if (!ReadFileAsString(filePath, text))
        return nullptr;

    auto root = std::make_unique<VdfNode>();
    Tokenizer tz(text);
    if (!ParseBlock(tz, *root))
        return nullptr;
    return root;
}
