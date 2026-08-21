#pragma once

#include <afx.h>
#include <afxwin.h>

// ---------------------------------------------------------------------------
// Theme —— Steam 截图管理器风格的暗色配色方案
//   主背景   #171A21  (Steam 窗口深蓝黑)
//   面板     #1B2838  (内容区)
//   卡片     #2A475E  (悬停/分隔)
//   强调蓝   #66C0F4  (选中、高亮文字)
//   主文字   #C7D5E0
//   次文字   #8F98A0
// ---------------------------------------------------------------------------
namespace Theme
{
    inline COLORREF Background()  { return RGB(0x17, 0x1A, 0x21); }
    inline COLORREF Panel()       { return RGB(0x1B, 0x28, 0x38); }
    inline COLORREF Card()        { return RGB(0x2A, 0x47, 0x5E); }
    inline COLORREF Accent()      { return RGB(0x66, 0xC0, 0xF4); }
    inline COLORREF Text()        { return RGB(0xC7, 0xD5, 0xE0); }
    inline COLORREF TextDim()     { return RGB(0x8F, 0x98, 0xA0); }
    inline COLORREF Selection()   { return RGB(0x35, 0x5E, 0x80); }

    // 各区域共用的画刷/字体(惰性创建,进程生命周期内复用)
    CBrush& BkBrush();        // 主背景画刷
    CBrush& PanelBrush();     // 面板画刷
    CFont&  Font();           // 常规文字
    CFont&  FontBold();       // 粗体标题
    CFont&  FontSmall();      // 小号说明文字
}
