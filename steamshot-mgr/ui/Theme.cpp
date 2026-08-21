#include "Theme.h"

namespace
{
    // 创建 Steam 风格字体;Segoe UI 为 Win11 系统字体
    CFont* MakeFont(int pointSize, int weight)
    {
        auto* font = new CFont();
        HDC hdc = ::GetDC(nullptr);
        int dpi = hdc ? ::GetDeviceCaps(hdc, LOGPIXELSY) : 96;
        if (hdc) ::ReleaseDC(nullptr, hdc);

        int lfHeight = -MulDiv(pointSize, dpi, 72);
        font->CreateFont(lfHeight, 0, 0, 0, weight, FALSE, FALSE, 0,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        return font;
    }
}

CBrush& Theme::BkBrush()
{
    static CBrush brush(Background());
    return brush;
}

CBrush& Theme::PanelBrush()
{
    static CBrush brush(Panel());
    return brush;
}

CFont& Theme::Font()
{
    static CFont* font = MakeFont(10, FW_NORMAL);
    return *font;
}

CFont& Theme::FontBold()
{
    static CFont* font = MakeFont(11, FW_SEMIBOLD);
    return *font;
}

CFont& Theme::FontSmall()
{
    static CFont* font = MakeFont(8, FW_NORMAL);
    return *font;
}
