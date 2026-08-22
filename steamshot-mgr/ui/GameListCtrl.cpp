#include "GameListCtrl.h"
#include "Theme.h"

BEGIN_MESSAGE_MAP(GameListCtrl, CListBox)
    ON_WM_MEASUREITEM_REFLECT()
    ON_WM_DRAWITEM_REFLECT()
    ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

void GameListCtrl::SetGames(const std::vector<GameShots>* games)
{
    m_games = games;
    ResetContent();
    if (games)
    {
        for (size_t i = 0; i < games->size(); ++i)
            AddString(L""); // 自绘模式下内容仅占位
    }
}

void GameListCtrl::MeasureItem(LPMEASUREITEMSTRUCT lpMis)
{
    lpMis->itemHeight = 44; // 两行文本的高度
}

void GameListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDis)
{
    if (!m_games || static_cast<size_t>(lpDis->itemID) >= m_games->size())
        return;

    CDC dc;
    dc.Attach(lpDis->hDC);
    CRect rc = lpDis->rcItem;
    const GameShots& game = (*m_games)[lpDis->itemID];

    bool selected = (lpDis->itemState & ODS_SELECTED) != 0;
    bool focused  = (lpDis->itemState & ODS_FOCUS) != 0;

    // 背景
    CBrush bk(selected ? Theme::Selection() : Theme::Panel());
    dc.FillRect(rc, &bk);
    if (selected)
    {
        // 左侧 3px 蓝色强调条
        CRect bar(rc.left, rc.top, rc.left + 3, rc.bottom);
        CBrush barBrush(Theme::Accent());
        dc.FillRect(bar, &barBrush);
    }

    dc.SetBkMode(TRANSPARENT);

    // 游戏名(未知名称时回退为 "App <AppID>")
    CString title = game.Name;
    if (title.IsEmpty())
        title.Format(L"App %u", game.AppId);

    CRect rcTitle(rc.left + 12, rc.top + 5, rc.right - 8, rc.top + 25);
    CFont* oldFont = dc.SelectObject(selected ? &Theme::FontBold() : &Theme::Font());
    dc.SetTextColor(selected ? Theme::Accent() : Theme::Text());
    dc.DrawText(title, rcTitle, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);

    // 截图数量 + AppID + 云端上传统计
    // 格式: "<AppID> · N 张截图" 或 "AppID · N 张 · ☁X ↑Y"
    CString count;
    if (game.Name.IsEmpty())
        count.Format(L"%d 张截图", static_cast<int>(game.Shots.size()));
    else
        count.Format(L"%u  ·  %d 张截图", game.AppId, static_cast<int>(game.Shots.size()));

    // 有混合状态时追加 ☁已传/↑未传 计数(全部已传或全未传时省略,保持简洁)
    const int total = static_cast<int>(game.Shots.size());
    if (game.UploadedCount > 0 && game.NotUploadedCount > 0)
    {
        CString cloud;
        cloud.Format(L"  ·  \u2601%d \u2191%d", game.UploadedCount, game.NotUploadedCount);
        count += cloud;
    }
    else if (game.UploadedCount == total && total > 0)
    {
        count += L"  ·  全部\u2601";
    }

    CRect rcCount(rc.left + 12, rc.top + 24, rc.right - 8, rc.bottom - 2);
    dc.SelectObject(&Theme::FontSmall());
    dc.SetTextColor(Theme::TextDim());
    dc.DrawText(count, rcCount, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    // 底部 1px 分隔线
    CPen pen(PS_SOLID, 1, RGB(0x10, 0x14, 0x1A));
    CPen* oldPen = dc.SelectObject(&pen);
    dc.MoveTo(rc.left, rc.bottom - 1);
    dc.LineTo(rc.right, rc.bottom - 1);
    dc.SelectObject(oldPen);

    dc.SelectObject(oldFont);
    dc.Detach();
}

HBRUSH GameListCtrl::CtlColor(CDC* pDC, UINT /*nCtlColor*/)
{
    pDC->SetBkColor(Theme::Panel());
    pDC->SetTextColor(Theme::Text());
    return Theme::PanelBrush();
}
