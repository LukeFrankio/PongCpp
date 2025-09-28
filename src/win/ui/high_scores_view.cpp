#include "high_scores_view.h"
#include "../../core/game_core.h"
#include "../highscores.h"
#include <windows.h>
#include <string>
#include <cwchar>

static void drawCenter(HDC dc,const std::wstring& t,int cx,int y){
	SIZE sz{0,0};
	GetTextExtentPoint32W(dc,t.c_str(),(int)t.size(),&sz);
	RECT r{cx - sz.cx/2,y - sz.cy/2,cx + sz.cx/2,y + sz.cy/2};
	DrawTextW(dc,t.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

void HighScoresView::begin(std::vector<HighScoreEntry>* list){ this->scores=list; hoverIndex=-1; }
bool HighScoresView::frame(HDC dc,int w,int h,int dpi,int mx,int my,bool click,bool deleteRequest,int* deletedIndex){
	if(!dc||!scores) return false; double s=(double)dpi/96.0;
	RECT bg{0,0,w,h}; HBRUSH b=CreateSolidBrush(RGB(18,18,26)); FillRect(dc,&bg,b); DeleteObject(b);
	SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(235,235,245)); drawCenter(dc,L"High Scores", w/2, (int)(40*s));
	int y0=(int)(90*s); int lineH=(int)(30*s); hoverIndex=-1;
	for(size_t i=0;i<scores->size() && i<10;i++){
		int cy=y0+(int)(i*lineH); std::wstring line=std::to_wstring(i+1)+L". "+(*scores)[i].name+L" - "+std::to_wstring((*scores)[i].score);
		RECT hit{ w/2 - (int)(240*s), cy-(int)(12*s), w/2 + (int)(240*s), cy+(int)(12*s) };
		if(mx>=hit.left && mx<=hit.right && my>=hit.top && my<=hit.bottom) hoverIndex=(int)i;
		bool selected = (int)i==hoverIndex; // selection is hover driven; on click we use hoverIndex snapshot
		if(selected){ HBRUSH hb=CreateSolidBrush(RGB(50,60,90)); FillRect(dc,&hit,hb); DeleteObject(hb);}        
		drawCenter(dc,line,w/2,cy);
	}
	// Delete button
	int btnW=(int)(180*s); int btnH=(int)(34*s); int btnX=w/2 - btnW/2; int btnY=h - (int)(110*s);
	RECT delR{btnX,btnY,btnX+btnW,btnY+btnH}; bool hoverDel = (mx>=delR.left && mx<=delR.right && my>=delR.top && my<=delR.bottom);
	HBRUSH db=CreateSolidBrush(hoverDel?RGB(120,40,40):RGB(70,30,30)); FillRect(dc,&delR,db); DeleteObject(db); FrameRect(dc,&delR,(HBRUSH)GetStockObject(GRAY_BRUSH));
	drawCenter(dc,L"Delete Selected", w/2, btnY+btnH/2);
	// Deletion triggers: (1) user clicked delete button (deleteRequest signalled by caller) while an index hovered
	// or (2) explicit deleteRequest key handling (VK_DELETE) with any hovered index
	if(deleteRequest && hoverIndex!=-1){
		if(hoverDel){ if(deletedIndex) *deletedIndex = hoverIndex; }
	}
	drawCenter(dc,L"Enter/Esc to close", w/2, h-(int)(50*s));
	return true;
}
