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
	int br=(std::max)(4, (int)(8*ui+0.5));
	// Draw all balls (first is primary, brighter)
	for (size_t bi=0; bi<gs.balls.size(); ++bi) {
		int bx = mapX(gs.balls[bi].x);
		int by = mapY(gs.balls[bi].y);
		COLORREF colMain = (bi==0)? RGB(250,220,220) : RGB(200,200,230);
		COLORREF colInner = (bi==0)? RGB(200,80,80) : RGB(120,120,200);
		HBRUSH ball=CreateSolidBrush(colMain); HBRUSH shade=CreateSolidBrush(colInner);
		HBRUSH oldB=(HBRUSH)SelectObject(dc,ball); Ellipse(dc,bx-br,by-br,bx+br,by+br); SelectObject(dc,shade); Ellipse(dc,bx-br/2,by-br/2,bx+br/2,by+br/2); SelectObject(dc,oldB);
		DeleteObject(ball); DeleteObject(shade);
	}
	if (gs.balls.empty()) {
		int bx=mapX(gs.ball_x), by=mapY(gs.ball_y);
		HBRUSH ball=CreateSolidBrush(RGB(250,220,220)); HBRUSH shade=CreateSolidBrush(RGB(200,80,80));
		HBRUSH oldB=(HBRUSH)SelectObject(dc,ball); Ellipse(dc,bx-br,by-br,bx+br,by+br); SelectObject(dc,shade); Ellipse(dc,bx-br/2,by-br/2,bx+br/2,by+br/2); SelectObject(dc,oldB);
		DeleteObject(ball); DeleteObject(shade);
	}

	// Obstacles
	if (gs.mode == GameMode::Obstacles) {
		HBRUSH obBrush = CreateSolidBrush(RGB(90,140,200)); HBRUSH old=(HBRUSH)SelectObject(dc, obBrush); HPEN nullPen=(HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
		for (auto &ob : gs.obstacles) {
			int left = mapX(ob.x - ob.w/2.0);
			int right = mapX(ob.x + ob.w/2.0);
			int top = mapY(ob.y - ob.h/2.0);
			int bottom = mapY(ob.y + ob.h/2.0);
			Rectangle(dc, left, top, right, bottom);
		}
		SelectObject(dc, nullPen); SelectObject(dc, old); DeleteObject(obBrush);
	}

	// Black holes
	if (!gs.blackholes.empty()) {
		for (auto &bh : gs.blackholes) {
			int cx = mapX(bh.x);
			int cy = mapY(bh.y);
			int radius = (std::max)(8, (int)(16*ui+0.5));
			
			// Draw outer glow/event horizon (dark purple)
			HBRUSH glowBrush = CreateSolidBrush(RGB(80, 40, 120));
			HBRUSH oldBrush = (HBRUSH)SelectObject(dc, glowBrush);
			HPEN oldPen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
			Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
			SelectObject(dc, oldPen);
			SelectObject(dc, oldBrush);
			DeleteObject(glowBrush);
			
			// Draw inner core (black)
			int innerRadius = radius / 2;
			HBRUSH coreBrush = CreateSolidBrush(RGB(0, 0, 0));
			oldBrush = (HBRUSH)SelectObject(dc, coreBrush);
			oldPen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
			Ellipse(dc, cx - innerRadius, cy - innerRadius, cx + innerRadius, cy + innerRadius);
			SelectObject(dc, oldPen);
			SelectObject(dc, oldBrush);
			DeleteObject(coreBrush);
		}
	}

	// Horizontal enemy paddles (top/bottom)
	if (gs.mode == GameMode::ThreeEnemies) {
		int halfW = (int)((gs.paddle_w / (double)gs.gw) * winW * 0.5);
		int topY = mapY(1.0);
		int bottomY = mapY(gs.gh - 2.0);
		int cxTop = mapX(gs.top_x);
		int cxBottom = mapX(gs.bottom_x);
		HBRUSH hpBrush = CreateSolidBrush(RGB(200,240,200)); HBRUSH old=(HBRUSH)SelectObject(dc,hpBrush); HPEN np2=(HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
		// Thickness scale with dpi
		int thick = (std::max)(4, (int)(8*ui+0.5));
		// Top paddle
		RECT rTop{cxTop-halfW, topY-thick/2, cxTop+halfW, topY+thick/2}; FillRect(dc,&rTop,hpBrush);
		int capR = thick/2;
		Ellipse(dc, rTop.left-capR, rTop.top, rTop.left+capR, rTop.bottom);
		Ellipse(dc, rTop.right-capR, rTop.top, rTop.right+capR, rTop.bottom);
		// Bottom paddle
		RECT rBot{cxBottom-halfW, bottomY-thick/2, cxBottom+halfW, bottomY+thick/2}; FillRect(dc,&rBot,hpBrush);
		Ellipse(dc, rBot.left-capR, rBot.top, rBot.left+capR, rBot.bottom);
		Ellipse(dc, rBot.right-capR, rBot.top, rBot.right+capR, rBot.bottom);
		SelectObject(dc,np2); SelectObject(dc,old); DeleteObject(hpBrush);
	}
}
