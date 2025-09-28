#include "classic_renderer.h"
#include "../../core/game_core.h"
#include <algorithm>

ClassicRenderer::ClassicRenderer() = default;
ClassicRenderer::~ClassicRenderer(){
	if(penThin) DeleteObject(penThin);
	if(penGlow) DeleteObject(penGlow);
}

void ClassicRenderer::ensureResources(int dpi){
	if(dpi == cachedDpi && penThin && penGlow) return;
	if(penThin) DeleteObject(penThin); penThin=nullptr;
	if(penGlow) DeleteObject(penGlow); penGlow=nullptr;
	double ui = (double)dpi/96.0;
	int thin = (std::max)(1, (int)(2*ui+0.5));
	int glow = (std::max)(3, (int)(6*ui+0.5));
	penThin = CreatePen(PS_SOLID, thin, RGB(200,200,200));
	penGlow = CreatePen(PS_SOLID, glow, RGB(100,100,120));
	cachedDpi = dpi;
}

void ClassicRenderer::onResize(int, int){ /* reserved */ }

void ClassicRenderer::render(const GameState& gs, HDC dc, int winW, int winH, int dpi) {
	if(!dc) return;
	ensureResources(dpi);
	// Clear background
	HBRUSH bg=(HBRUSH)GetStockObject(BLACK_BRUSH); RECT r{0,0,winW,winH}; FillRect(dc,&r,bg);

	// Center dashed line (glow + thin overlay)
	HPEN oldPen = (HPEN)SelectObject(dc, penGlow);
	double ui = (double)dpi/96.0;
	int dashH = (std::max)(12, (int)(20*ui+0.5));
	int dashSeg = (std::max)(6, (int)(10*ui+0.5));
	for(int y=0;y<winH;y+=dashH){ MoveToEx(dc,winW/2,y,nullptr); LineTo(dc,winW/2,y+dashSeg);} 
	SelectObject(dc, penThin);
	for(int y=0;y<winH;y+=dashH){ MoveToEx(dc,winW/2,y,nullptr); LineTo(dc,winW/2,y+dashSeg);} 
	SelectObject(dc, oldPen);

	// Map game coords
	auto mapX=[&](double gx){ return (int)(gx/gs.gw*winW); };
	auto mapY=[&](double gy){ return (int)(gy/gs.gh*winH); };

	// Paddles
	int px1=mapX(1), px2=mapX(3);
	RECT pr; pr.left=px1; pr.right=px2; pr.top=mapY(gs.left_y); pr.bottom=mapY(gs.left_y+gs.paddle_h);
	HBRUSH pb=CreateSolidBrush(RGB(240,240,240)); HBRUSH ob=(HBRUSH)SelectObject(dc,pb);
	HPEN np=(HPEN)GetStockObject(NULL_PEN); HPEN op=(HPEN)SelectObject(dc,np);
	FillRect(dc,&pr,pb); int rad=(std::max)(1, (int)(((pr.right-pr.left)/2.0)*ui+0.5));
	Ellipse(dc,pr.left-rad,pr.top,pr.left+rad,pr.bottom); Ellipse(dc,pr.right-rad,pr.top,pr.right+rad,pr.bottom);
	SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pb);

	int rx1=mapX(gs.gw-3), rx2=mapX(gs.gw-1); pr.left=rx1; pr.right=rx2; pr.top=mapY(gs.right_y); pr.bottom=mapY(gs.right_y+gs.paddle_h);
	HBRUSH pb2=CreateSolidBrush(RGB(240,240,240)); HBRUSH ob2=(HBRUSH)SelectObject(dc,pb2); HPEN op2=(HPEN)SelectObject(dc,np);
	FillRect(dc,&pr,pb2); Ellipse(dc,pr.left-rad,pr.top,pr.left+rad,pr.bottom); Ellipse(dc,pr.right-rad,pr.top,pr.right+rad,pr.bottom);
	SelectObject(dc,op2); SelectObject(dc,ob2); DeleteObject(pb2);

	// Ball
	int bx=mapX(gs.ball_x), by=mapY(gs.ball_y); int br=(std::max)(4, (int)(8*ui+0.5));
	HBRUSH ball=CreateSolidBrush(RGB(250,220,220)); HBRUSH shade=CreateSolidBrush(RGB(200,80,80));
	HBRUSH oldB=(HBRUSH)SelectObject(dc,ball); Ellipse(dc,bx-br,by-br,bx+br,by+br); SelectObject(dc,shade); Ellipse(dc,bx-br/2,by-br/2,bx+br/2,by+br/2); SelectObject(dc,oldB);
	DeleteObject(ball); DeleteObject(shade);
}
