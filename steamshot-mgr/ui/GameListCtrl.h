#pragma once

#include <afxwin.h>
#include <vector>
#include "../core/ScreenshotStore.h"

// ---------------------------------------------------------------------------
// GameListCtrl —— 左侧游戏列表(自绘,Steam 风格)
//   每项两行:游戏名(亮) + "N 张截图"(暗)
//   选中项高亮为 Steam 蓝
// ---------------------------------------------------------------------------
class GameListCtrl : public CListBox
{
public:
    void SetGames(const std::vector<GameShots>* games);

protected:
    afx_msg void MeasureItem(LPMEASUREITEMSTRUCT lpMis);
    afx_msg void DrawItem(LPDRAWITEMSTRUCT lpDis);
    afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
    DECLARE_MESSAGE_MAP()

private:
    const std::vector<GameShots>* m_games = nullptr;
};
