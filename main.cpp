/*
  StealthMan
  Copyright (C) 2014-2019 Bernhard Schelling

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ZL_Application.h>
#include <ZL_Display.h>
#include <ZL_Surface.h>
#include <ZL_Signal.h>
#include <ZL_Audio.h>
#include <ZL_Font.h>
#include <ZL_Scene.h>
#include <ZL_Timer.h>
#include <ZL_Particles.h>
#include <ZL_Math.h>
#include <ZL_Network.h>
#include <ZL_SynthImc.h>
#include <iostream>
#include <map>
#include <vector>
#include <list>
using namespace std;

static const int lw = 40; //1280/32
static const int lh = 22; //720/32
static const char layout[] =
	"# #################################### #"
	"#-----------------*##*-----------------#"
	"#-#-###-#####-####-##-####-#####-###-#-#"
	"#-#-###-##--------------------##-###-#-#"
	"#-#-###-##-#-#-##########-#-#-##-###-#-#"
	"#---------*#-#----####----#-#*---------#"
	"#---#-#-####-###--####--###-####-#-#---#"
	"#-#---#------#------------#------#---#-#"
	"#-#-#-#---##-#--###  ###--#-##---#-#-#-#"
	"#-#-#---#-------#      #-------#---#-#-#"
	"#-#-#-#-#-#--#--#      #--#--#-#-#-#-#-#"
	" ---#-#-#-#-##--########--##-#-#-#-#--- "
	"#-#-#-#-----------    -----------#-#-#-#"
	"#-#-#---#-######-######-######-#---#-#-#"
	"#-#---#------------##------------#---#-#"
	"#-#*--####-#######-##-#######-####--*#-#"
	"#-###------------------------------###-#"
	"#-#####-###-###-##-##-##-###-###-#####-#"
	"#--*###-----#-#---*##*---#-#-----###*--#"
	"#-#-###-###-###-########-###-###-###-#-#"
	"#--------------------------------------#"
	"# #################################### #";
//   0_________1_________2_________3_________

bool pellets[lw*lh], powerpellets[lw*lh];
ticks_t pickuptimes[lw*lh];

static inline int layoutidx(scalar x, scalar y) { return (x > -0.5 && y > -0.5 && x < s(lw)-0.5 && y < s(lh)-0.5 ? (lh-1-(int)(y+0.5001))*lw+(int)(x+0.5001) : -1); }
static inline bool layoutcoll(scalar x, scalar y) { return (x > -0.5 && y > -0.5 && x < s(lw)-0.5 && y < s(lh)-0.5 && (layout[(lh-1-(int)(y+0.5001))*lw+(int)(x+0.5001)] == '#')); }
static scalar PLAYERSIZE = s(.49);
extern ZL_SynthImcTrack imcMusic, imcEat, imcPower, imcDead;
extern TImcSongData imcDataIMCPAK;
static ZL_Sound sndPak;
static ZL_Font fnt, fntTitle;
static ZL_Surface srfSprite, srfLightMask, srfLudumDare;

static int timefreeze, lowfpscount;
static scalar elapsesum;
static ticks_t timeTick, timeLastPellet, timeLastPowerpelletStart, timeLastPowerpelletEnd, timeLastEnemy, timeEnemyRespawn, zlticksDead;

#define TIMESPAN(from,to) ((int)(to-(ticks_t)(from)))
#define TIMEUNTIL(tm) TIMESPAN(timeTick, tm)
#define TIMESINCE(tm) TIMESPAN(tm, timeTick)
#define POWERPELLETTIMELEN 10000
#define ENEMYRESPAWNTIMERSTART 10000
#define PELLETRESTPAWNTIME 30000
#define POWERPELLETRESTPAWNTIME 60000

static const ZL_Color colPPGhost = ZLRGBA(0,0,.8,.8);
static const ZL_Color colPlayerLight = ZLRGB(.4,.35,.1);
static bool PPActive, PPActiveBlink, ArcadeMode = false;
static ZL_Rectf recScreen(-s(.5), -s(.5), s(lw)-s(.5), s(lh)-s(.5));
static ZL_Rectf recButtonStealth(640-250, 270, 640+250, 270+80);
static ZL_Rectf recButtonArcade(640-250, 150, 640+250, 150+80);

#ifdef __SMARTPHONE__
bool useTouchUI = true;
#else
bool useTouchUI = false;
#endif

static struct sPlayer
{
	ZL_Vector pos, oldpos, dir;
	scalar ang, speed, mainspeed, bonusspeed, maintargetspeed, bonustargetspeed;
	int score;
	bool gotPellet, preferVerticalMovement, dead;
	void Setup(ZL_Vector p)
	{
		pos = oldpos = p;
		mainspeed = maintargetspeed = s(0.10);
		bonusspeed = bonustargetspeed = 0;
		gotPellet = false;
		score = 0;
		dead = false;
	}
	void Score(int add)
	{
		scalar sf = s(add + 4 / 5);
		maintargetspeed += 0.0001f*sf;
		bonustargetspeed += 0.001f*sf;
		score += (ArcadeMode ? add : add*2);
		timeEnemyRespawn -= add*10;
		if (timeEnemyRespawn < 500) timeEnemyRespawn = 500;
	}
	void Update()
	{
		if (dead) return;
		if (bonustargetspeed > 0) bonustargetspeed -= bonustargetspeed*s(.01);
		mainspeed += (maintargetspeed - mainspeed)*s(.01);
		bonusspeed += (bonustargetspeed - bonusspeed)*s(.01);
		speed = ZL_Math::Min(mainspeed + bonusspeed, s(0.4));

		bool oldhor = (dir.x!=0), oldvert = (dir.y!=0);
		dir.x = (ZL_Display::KeyDown[ZLK_A] || ZL_Display::KeyDown[ZLK_LEFT] ? s(-1) : s(0)) + (ZL_Display::KeyDown[ZLK_D] || ZL_Display::KeyDown[ZLK_RIGHT] ? s(1) : s(0));
		dir.y = (ZL_Display::KeyDown[ZLK_S] || ZL_Display::KeyDown[ZLK_DOWN] ? s(-1) : s(0)) + (ZL_Display::KeyDown[ZLK_W] || ZL_Display::KeyDown[ZLK_UP   ] ? s(1) : s(0));

		ZL_Vector touchPosPlayerOnScreen = ZL_Rectf::Map(pos, recScreen, ZL_Rectf(0, 0, ZLWIDTH, ZLHEIGHT));
		ZL_Rectf touchRecPlayer = ZL_Rectf(touchPosPlayerOnScreen, ZLHEIGHT*.01f);
		if (!dir) dir = (ZL_Display::MouseDown[ZL_BUTTON_LEFT] && !touchRecPlayer.Contains(ZL_Display::PointerPos()) ? ((ZL_Display::PointerPos() - touchPosPlayerOnScreen) / (ZLHEIGHT*.2f)).SetMaxLength(1) : ZL_Vector::Zero);

		if (pos.x <= s(-.5) || pos.x >= s(lw-.5)) dir.y = 0;
		if (pos.y <= s(-.5) || pos.y >= s(lh-.5)) dir.x = 0;
		if      (!oldhor && dir.x!=0) preferVerticalMovement = false;
		else if (!oldvert && dir.y!=0) preferVerticalMovement = true;
		pos += dir*speed;
		if (!Collision() && oldpos != pos)
		{
			pos = oldpos + (pos - oldpos).SetLength(speed);
		}
		if (oldpos != pos)
		{
			if (pos.x < s(-0.95))   { pos.x = s(lw-0.05); pos.y = oldpos.y; }
			if (pos.x > s(lw-0.05)) { pos.x = s(-0.95);   pos.y = oldpos.y; }
			if (pos.y < s(-0.95))   { pos.y = s(lh-0.05); pos.x = oldpos.x; }
			if (pos.y > s(lh-0.05)) { pos.y = s(-0.95);   pos.x = oldpos.x; }
			if (pos.Far(oldpos, 0.01f)) ang = (pos-oldpos).GetAngle();
		}
		oldpos = pos;
		int idx = layoutidx(pos.x, pos.y);
		if (idx >= 0 && pellets[idx])
		{
			pellets[idx] = false;
			pickuptimes[idx] = timeTick;
			Score(1);
			gotPellet = true;
			//sndPak.Play();
		}
		if (idx >= 0 && powerpellets[idx])
		{
			powerpellets[idx] = false;
			pickuptimes[idx] = timeTick;
			Score(10);
			imcPower.Play(true);
			if (!PPActive) timeLastPowerpelletStart = timeTick;
			timeLastPowerpelletEnd = timeTick+POWERPELLETTIMELEN;
		}
	}
	bool Collision()
	{
		#if defined(ZILLALOG)
		if (ZL_Display::KeyDown[ZLK_LSHIFT]) return false;
		#endif
		if (preferVerticalMovement) return ((dir.y && CollisionVertical()) | (dir.x && CollisionHorizontal()));
		return ((dir.x && CollisionHorizontal()) | (dir.y && CollisionVertical()));
	}
	bool CollisionHorizontal()
	{
		scalar cx = pos.x+dir.x*PLAYERSIZE;
		if (layoutcoll(cx, pos.y)) { pos.x += ((int)cx+(dir.x < 0 ? 1 : 0)-cx+dir.x*s(.5)); return true; }
		if (DiagCollFix(cx, pos.y+PLAYERSIZE, 0, +1)) return true;
		if (DiagCollFix(cx, pos.y-PLAYERSIZE, 0, -1)) return true;
		return false;
	}
	bool CollisionVertical()
	{
		scalar cy = pos.y+dir.y*PLAYERSIZE;
		if (layoutcoll(pos.x, cy)) { pos.y += ((int)cy+(dir.y < 0 ? 1 : 0)-cy+dir.y*s(.5)); return true; }
		if (DiagCollFix(pos.x+PLAYERSIZE, cy, +1, 0)) return true;
		if (DiagCollFix(pos.x-PLAYERSIZE, cy, -1, 0)) return true;
		return false;
	}
	bool DiagCollFix(scalar cx, scalar cy, scalar dirx, scalar diry)
	{
		if (!layoutcoll(cx, cy)) return false;
		ZL_Vector w(sfloor(cx)+s(.5), sfloor(cy)+s(.5)), p;
		if (!ZL_Math::LineCircleCollision(w, ZLV(w.x+dirx, w.y+diry), pos, PLAYERSIZE, &p)) return false;
		if (dirx) pos.x += w.x - p.x;
		if (diry) pos.y += w.y - p.y;
		pos = oldpos + (pos - oldpos).SetLength(speed);
		return true;
	}

	void Draw()
	{
		if (zlticksDead && ZLSINCE(zlticksDead) > 1000) return;
		srfSprite.SetTilesetIndex(0);
		srfSprite.Draw(pos, s(0.055), s(0.055), ZLBLACK);
		if (zlticksDead)
		{
			int t = ZLSINCE(zlticksDead)*6/1000;
			srfSprite.SetTilesetIndex(t > 3 ? 10+t : 1+t*2);
		}
		else
		{
			int t = (int)(s(timeTick)/(s(5)/speed))%14;
			srfSprite.SetTilesetIndex(t > 7 ? 14-t : t);
		}
		srfSprite.Draw(pos, ang, ZL_Color::Yellow);
	}
} Player;

void DrawWalls(const ZL_Color& col)
{
	const char *lp = layout;
	for (scalar y = s(lh)-s(1.5); y > -1; y--)
	{
		for (scalar x = -s(.5); x < lw-1; x++, lp++)
		{
			if (*lp == '#')
			{
				ZL_Display::FillRect(x, y, x+1, y+1, col);
			}
		}
	}
}

void GenerateLightMap(ZL_Surface srfLightMapSwaps[2], scalar srfSize, const ZL_Vector& pos, bool WithLightMask = true)
{
	scalar LPX = (pos.x+s(.5)) / s(lw), LPY = (pos.y+s(.5)) / s(lh);
	srfLightMapSwaps[0].RenderToBegin(false, false);
	ZL_Display::ClearFill(ZLWHITE);
	ZL_Display::PushOrtho(-s(.5), s(lw)-s(.5), -s(.5), s(lh)-s(.5));
	DrawWalls(ZLBLACK);
	ZL_Display::PopOrtho();
	srfLightMapSwaps[0].RenderToEnd();
	srfLightMapSwaps[1].RenderToBegin(true);
	srfLightMapSwaps[0].DrawTo(0, 0, srfSize, srfSize);
	srfLightMapSwaps[1].RenderToEnd();
	scalar lmw = srfSize/4, lmh = s(lmw / 9 * 16);
	for (scalar s = 2; s < 2000; s *= 2)
	{
		srfLightMapSwaps[0].RenderToBegin();
		if (WithLightMask) srfLightMask.DrawTo(srfSize*LPX-lmw, srfSize*LPY-lmh, srfSize*LPX+lmw, srfSize*LPY+lmh, ZLALPHA(.03));
		ZL_Display::SetBlendFunc(ZL_Display::BLEND_ZERO, ZL_Display::BLEND_SRCCOLOR);
		srfLightMapSwaps[1].DrawTo(0-s*LPX, 0-s*LPY, srfSize+s*(1-LPX), srfSize+s*(1-LPY));
		ZL_Display::ResetBlendFunc();
		srfLightMapSwaps[0].RenderToEnd();
		srfLightMapSwaps[1].RenderToBegin();
		srfLightMapSwaps[0].DrawTo(0, 0, srfSize, srfSize);
		srfLightMapSwaps[1].RenderToEnd();
	}
}

struct sEnemy
{
	ZL_Vector pos, dir, targ;
	scalar speed;
	ZL_Color col;
	ZL_Surface srfLightMapSwaps[2];
	bool visible;
	sEnemy(const ZL_Vector& p, const ZL_Color& c) : pos(p), col(c)
	{
		srfLightMapSwaps[0] = ZL_Surface(128, 128);
		srfLightMapSwaps[1] = ZL_Surface(128, 128);
		FindTarg();
		speed = RAND_RANGE(.06, .09)+(s(timeTick)/s(600000));
		if (speed > 0.22f) speed = 0.22f;
		//speed = .2157; //test max speed
	}

	void FindTarg()
	{
		for (;;)
		{
			int d = RAND_INT_RANGE(0, 3);
			ZL_Vector newdir = (d == 0 ? ZL_Vector::Right : (d == 1 ? ZL_Vector::Up : (d == 2 ? -ZL_Vector::Right : -ZL_Vector::Up)));
			ZL_Vector perp = newdir.VecPerp();
			if (newdir == -dir) continue;
			targ = pos;
			int steps;
			for (steps = 0; steps < 10; steps++)
			{
				if (
					(newdir.x < 0 && targ.x < s(1.5)) ||
					(newdir.x > 0 && targ.x > s(lw-1-1.5)) ||
					(newdir.y < 0 && targ.y < s(1.5)) ||
					(newdir.y > 0 && targ.y > s(lh-1-1.5)) ||
					layoutcoll(targ.x+newdir.x, targ.y+newdir.y)) break;
				targ += newdir;
				if (steps && (!layoutcoll(targ.x+perp.x, targ.y+perp.y) || !layoutcoll(targ.x+perp.x, targ.y+perp.y))) break;
			}
			if (!steps) continue;
			dir = newdir;
			return;
		}
	}

	void Update()
	{
		pos += dir*speed;
		if (pos.x < s(   1)) { pos.x = s(   1); FindTarg(); }
		if (pos.x > s(lw-2)) { pos.x = s(lw-2); FindTarg(); }
		if (pos.y < s(   1)) { pos.y = s(   1); FindTarg(); }
		if (pos.y > s(lh-2)) { pos.y = s(lh-2); FindTarg(); }
		if (pos.Near(targ, speed*s(1.5)))
		{
			pos = targ;
			FindTarg();
		}
		visible = (pos.Near(Player.pos, 10));
	}

	void DrawLightGenerate()
	{
		GenerateLightMap(srfLightMapSwaps, 128, pos);//, false);
	}
	static inline void DrawLightStart()
	{
		ZL_Display::SetBlendFunc(ZL_Display::BLEND_SRCCOLOR, ZL_Display::BLEND_ONE); //ADD ONLY WHITE
	}
	void DrawLightDo()
	{
		srfLightMapSwaps[1].DrawTo(recScreen, (PPActive ? colPPGhost : col));
	}
	static inline void DrawLightEnd()
	{
		ZL_Display::ResetBlendFunc();
	}

	static inline void DrawCharStart()
	{
		srfSprite.SetTilesetIndex(8+((ZLTICKS/100)%3));
	}
	void DrawChar()
	{
		srfSprite.Draw(pos, s(0.055), s(0.055), ZLBLACK);
		srfSprite.Draw(pos, (PPActiveBlink ? colPPGhost : col));
	}
	static inline void DrawEyeStart()
	{
		srfSprite.SetTilesetIndex(11);
	}
	void DrawEye()
	{
		srfSprite.Draw(pos);
	}
	static inline void DrawRevealSpriteStart(scalar a)
	{
		srfSprite.BatchRenderBegin(true);
		srfSprite.SetAlpha(a);
	}
	static inline void DrawRevealSpriteEnd()
	{
		srfSprite.SetAlpha(1);
		srfSprite.BatchRenderEnd();
	}

	static inline void DrawRevealLightStart()
	{
		ZL_Display::SetBlendFunc(ZL_Display::BLEND_SRCALPHA, ZL_Display::BLEND_ONE);
	}
	void DrawRevealLight(scalar a)
	{
		srfLightMapSwaps[1].DrawTo(recScreen, ZLCOLA(colPPGhost, a/2));
	}
	static inline void DrawRevealLightEnd()
	{
		ZL_Display::ResetBlendFunc();
	}
};

static ZL_Surface srfLightMapSwaps[2];
static vector<sEnemy> Enemies;
static bool istitle;

struct sPakuman : public ZL_Application
{
	sPakuman() : ZL_Application(60) { }

	virtual void Load(int argc, char *argv[])
	{
		if (!LoadReleaseDesktopDataBundle()) return;
		ZL_Display::Init("StealthMan", 1280, 720, ZL_DISPLAY_ALLOWRESIZEHORIZONTAL);
		ZL_Display::SetAA(true);
		ZL_Audio::Init();
		fnt = ZL_Font("Data/typomoderno.ttf.zip", 20);
		fntTitle = ZL_Font("Data/graycat.ttf.zip", 60);
		sndPak = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCPAK);

		ZL_Display::sigKeyDown.connect(this, &sPakuman::OnKeyDown);
		ZL_Display::sigPointerDown.connect(this, &sPakuman::OnPointerDown);
		srfSprite = ZL_Surface("Data/sprite.png").SetTilesetClipping(4, 4).SetOrigin(ZL_Origin::Center).SetScale(s(.05));
		srfLightMask = ZL_Surface("Data/light_mask.png");
		srfLudumDare = ZL_Surface("Data/ludumdare31.png").SetOrigin(ZL_Origin::BottomRight);

		srfLightMapSwaps[0] = ZL_Surface(512, 512);
		srfLightMapSwaps[1] = ZL_Surface(512, 512);

		GoToTitle();
		imcMusic.Play(true);
	}

	void GoToTitle()
	{
		istitle = true;
		Enemies.clear();
		memset(pellets, 0, sizeof(pellets));
		memset(powerpellets, 0, sizeof(powerpellets));
		Reset();
		Enemies.push_back(sEnemy(ZLV(16,14), ZL_Color::Red));
		Enemies.push_back(sEnemy(ZLV(23,14), ZL_Color::Cyan));
		Enemies.push_back(sEnemy(ZLV(15,12), ZLRGB(1,.5,.75)));
		Enemies.push_back(sEnemy(ZLV(24,12), ZL_Color::Orange));
	}

	void OnKeyDown(ZL_KeyboardEvent& e)
	{
		if (e.key != ZLK_ESCAPE) useTouchUI = false;
		if (e.key == ZLK_ESCAPE) { if (istitle) { Quit(); } else { GoToTitle(); } }
		if (istitle && (e.key == ZLK_DOWN || e.key == ZLK_UP || e.key == ZLK_W || e.key == ZLK_S)) { ArcadeMode ^= 1; sndPak.Play(); }
		if (istitle && (e.key == ZLK_RETURN || e.key == ZLK_SPACE)) { Start(); imcEat.Play(); }
		if (zlticksDead && ZLSINCE(zlticksDead) > 500) { GoToTitle(); sndPak.Play(); }
		#if defined(ZILLALOG)
		if (e.key == ZLK_E) AddEnemy();
		#endif
	}

	void OnPointerDown(ZL_PointerPressEvent& e)
	{
		useTouchUI = true;
		if (istitle && recButtonArcade.Contains(e))  { if ( ArcadeMode) { Start(); imcEat.Play(); } else { ArcadeMode = true;  sndPak.Play(); } }
		if (istitle && recButtonStealth.Contains(e)) { if (!ArcadeMode) { Start(); imcEat.Play(); } else { ArcadeMode = false; sndPak.Play(); } }
		if (zlticksDead && ZLSINCE(zlticksDead) > 500) { GoToTitle(); sndPak.Play(); }
	}

	void DrawTitle()
	{
		ZL_Color col = ZLHSV((ZLTICKS%6000)/s(6000),1,1);
		ZL_Color colsel = col*s(.3);
		DrawTextBordered(ZLHALFW, ZLHEIGHT*.75f, "StealthMan", 3.0f, 4, ZLBLACK, col);
		ZL_Display::FillRect(recButtonStealth.left-5,recButtonStealth.low-5,recButtonStealth.right+5,recButtonStealth.high+5, ZLWHITE);
		ZL_Display::FillRect(recButtonStealth.left-4,recButtonStealth.low-4,recButtonStealth.right+4,recButtonStealth.high+4, col);
		ZL_Display::FillRect(recButtonStealth, (ArcadeMode ? ZLBLACK : colsel));
		DrawTextBordered(recButtonStealth.MidX(), recButtonStealth.low+35, "Stealth Mode", 1.0f, 2, ZLBLACK, col);
		ZL_Display::FillRect(recButtonArcade.left-5,recButtonArcade.low-5,recButtonArcade.right+5,recButtonArcade.high+5, ZLWHITE);
		ZL_Display::FillRect(recButtonArcade.left-4,recButtonArcade.low-4,recButtonArcade.right+4,recButtonArcade.high+4, col);
		ZL_Display::FillRect(recButtonArcade, (ArcadeMode ? colsel : ZLBLACK));
		DrawTextBordered(recButtonArcade.MidX(), recButtonArcade.low+35, "Arcade Mode", 1.0f, 2, ZLBLACK, col);

		if (useTouchUI)
		{
			fnt.Draw(ZLHALFW, 90, "TAP AND HOLD TO MOVE", ZL_Origin::Center);
			fnt.Draw(ZLHALFW, 70, "SELECT MODE TO START", ZL_Origin::Center);
		}
		else
		{
			fnt.Draw(ZLHALFW, 90, "ARROW KEYS / WASD TO MOVE", ZL_Origin::Center);
			fnt.Draw(ZLHALFW, 70, "PRESS ENTER TO START", ZL_Origin::Center);
		}

		DrawTextBordered(25, 13, "(c) 2014-2019 Bernhard Schelling", s(.65), 1, ZLLUMA(.0,.95), ZLLUMA(1,.6), ZL_Origin::TopLeft);
		srfLudumDare.Draw(ZLFROMW(10), 10);
	}

	void DrawGameOver()
	{
		DrawTextBordered(ZLHALFW, ZLHEIGHT*.7f, "Game Over", 2.5f, 3, ZLBLACK, ZLWHITE);
		ZL_Rectf recScore(ZLHALFW-300, ZLHALFH-25, ZLHALFW+300, ZLHALFH+25);
		ZL_Display::FillRect(recScore.left-4,recScore.low-4,recScore.right+4,recScore.high+4, ZLWHITE);
		ZL_Display::FillRect(recScore, ZLBLACK);
		fntTitle.Draw(recScore.Center(), ZL_String::format("HIGHSCORE: %d", Player.score), 0.5f, ZL_Origin::Center);
	}

	void AddEnemy()
	{
		Enemies.push_back(sEnemy(ZLV(RAND_INT_RANGE(17,22),RAND_INT_RANGE(11,12)), RAND_COLOR));
	}

	void Start()
	{
		Reset();
		Enemies.clear();
		Enemies.push_back(sEnemy(ZLV(18,12), ZL_Color::Red));
		Enemies.push_back(sEnemy(ZLV(21,12), ZL_Color::Cyan));
		Enemies.push_back(sEnemy(ZLV(18,11), ZLRGB(1,.4,.3)));
		Enemies.push_back(sEnemy(ZLV(21,11), ZL_Color::Orange));
		Player.Setup(ZLV(19.5, 9));
		istitle = false;
		imcMusic.Play(true);
	}

	void Reset()
	{
		PPActive = false;
		PPActiveBlink = false;
		elapsesum = s(1.0/60.0/2.0);
		timeTick = 0;
		zlticksDead = 0;
		timeLastPellet = 0;
		timeLastEnemy = 0;
		timeEnemyRespawn = ENEMYRESPAWNTIMERSTART; //10s
		timeLastPowerpelletStart = 0;
		timeLastPowerpelletEnd = 0;
		timefreeze = 0;
		memset(pellets, 0, sizeof(pellets));
		memset(pickuptimes, 0, sizeof(pickuptimes));
		memset(powerpellets, 0, sizeof(powerpellets));
		const char *lp = layout;
		for (int i = 0; i < lw*lh; i++)
		{
			if (lp[i] == '-') pellets[i] = true;
			else if (lp[i] == '*') powerpellets[i] = true;
		}
	}

	void UpdateTitle()
	{
		sEnemy *pEnemy, *pEnemyBegin = &*Enemies.begin(), *pEnemyEnd = pEnemyBegin+Enemies.size();
		for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->Update();
	}

	void Update()
	{
		for (elapsesum += ZLELAPSED; elapsesum >= s(1.0/60.0); elapsesum -= s(1.0/60.0))
		{
			Player.Update();
			sEnemy *pEnemy, *pEnemyBegin = &*Enemies.begin(), *pEnemyEnd = pEnemyBegin+Enemies.size();
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->Update();
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy)
			{
				if (pEnemy->visible && Player.pos.Near(pEnemy->pos, PLAYERSIZE*1.8f))
				{
					if (PPActive)
					{
						if (Enemies.size() < 4 && timeTick > timeEnemyRespawn) timeLastEnemy = timeTick - timeEnemyRespawn + 500;
						Enemies.erase(Enemies.begin()+(pEnemy-pEnemyBegin));
						Player.Score(20);
						imcEat.Play(true);
						timefreeze = 15;
					}
					else
					{
						zlticksDead = ZLTICKS;
						imcDead.Play(true);
					}
					break;
				}
			}
		}
		if (TIMESINCE(timeLastEnemy) >= (int)timeEnemyRespawn)
		{
			if (Enemies.size() < 20) AddEnemy();
			if (Enemies.size() < 4 && timeTick > timeEnemyRespawn) timeLastEnemy = timeTick - timeEnemyRespawn + 500;
			else timeLastEnemy = timeTick;
		}

		for (int idx = 0; idx < lw*lh; idx++)
			if      (layout[idx] == '-' && !pellets[idx]      && TIMESINCE(pickuptimes[idx]) > PELLETRESTPAWNTIME) pellets[idx] = true;
			else if (layout[idx] == '*' && !powerpellets[idx] && TIMESINCE(pickuptimes[idx]) > POWERPELLETRESTPAWNTIME) powerpellets[idx] = true;

		if (timeTick < 80) return;
		static int lastsubtick = 0;
		int subtick = ((timeTick-80)%200);
		if (subtick < lastsubtick && Player.gotPellet)
		{
			if (Player.gotPellet)
			{
				sndPak.Play();
				timeLastPellet = timeTick-subtick;
				Player.gotPellet = false;
			}
		}
		lastsubtick = subtick;

		PPActive = (timeLastPowerpelletEnd && timeTick<timeLastPowerpelletEnd);
		PPActiveBlink = (PPActive && (TIMEUNTIL(timeLastPowerpelletEnd)>2000 || ((int)(ZL_Easing::OutQuad(TIMEUNTIL(timeLastPowerpelletEnd)/s(2000))*15))%2));
	}

	void DrawPellets(scalar alpha = 1)
	{
		if (alpha != 1) srfSprite.SetAlpha(alpha);
		srfSprite.BatchRenderBegin();
		srfSprite.SetTilesetIndex(13);
		int idx, x, y;
		for (idx = 0, y = lh-1; y >= 0; y--)
			for (x = 0; x < lw; x++, idx++)
				if (pellets[idx]) srfSprite.Draw(s(x), s(y));
				else if (layout[idx] == '-' && TIMESINCE(pickuptimes[idx]) > PELLETRESTPAWNTIME) pellets[idx] = true;

		srfSprite.SetTilesetIndex(12);
		for (idx = 0, y = lh-1; y >= 0; y--)
			for (x = 0; x < lw; x++, idx++)
				if (layout[idx] != '*') { }
				else if (powerpellets[idx]) srfSprite.Draw(s(x), s(y));
				else if (TIMESINCE(pickuptimes[idx]) > POWERPELLETRESTPAWNTIME) pellets[idx] = true;
		srfSprite.BatchRenderEnd();
		if (alpha != 1) srfSprite.SetAlpha(1);
	}

	virtual void AfterFrame()
	{
		if (lowfpscount < 10)
		{
			if (FPS < 10 && FrameCount > 3) lowfpscount++;
			else if (lowfpscount) lowfpscount--;
		}

		if (zlticksDead) { }
		else if (timefreeze && --timefreeze) { }
		else
		{
			timeTick += ZLELAPSEDTICKS;
			(istitle ? UpdateTitle() : Update());
		}

		bool multilight = (istitle || ArcadeMode);
		#if defined(ZILLALOG)
		if (ZL_Display::KeyDown[ZLK_LSHIFT]) multilight = true;
		#endif

		ZL_Display::ClearFill(multilight ? ZLBLACK : colPlayerLight);

		ZL_Display::PushOrtho(recScreen);

		GenerateLightMap(srfLightMapSwaps, 512, Player.pos);

		sEnemy *pEnemy, *pEnemyBegin = &*Enemies.begin(), *pEnemyEnd = pEnemyBegin+Enemies.size();
		if (lowfpscount < 10)
		{
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy)
				if (PPActive || pEnemy->visible || multilight)
					pEnemy->DrawLightGenerate();
			if (!PPActive)
			{
				sEnemy::DrawLightStart();
				for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy)
					if (PPActive || pEnemy->visible || multilight)
						pEnemy->DrawLightDo();
				sEnemy::DrawLightEnd();
			}
		}

		if (!istitle)
		{
			ZL_Display::SetBlendFunc(ZL_Display::BLEND_ONE, ZL_Display::BLEND_ONE); //ADD ONLY WHITE
			srfLightMapSwaps[1].DrawTo(recScreen, colPlayerLight);
			ZL_Display::ResetBlendFunc();
		}

		DrawPellets();

		srfSprite.BatchRenderBegin(true);
		sEnemy::DrawCharStart();
		for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawChar();
		sEnemy::DrawEyeStart();
		for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawEye();
		srfSprite.BatchRenderEnd();
		if (!multilight)
		{
			ZL_Display::SetBlendFunc(ZL_Display::BLEND_ZERO, ZL_Display::BLEND_SRCCOLOR); //ADD ONLY BLACK
			srfLightMapSwaps[1].DrawTo(recScreen, ZL_Color::White);
			ZL_Display::ResetBlendFunc();
		}

		if (!istitle) Player.Draw();

		if (multilight) DrawWalls(ZLRGB(.1,.1,.8));

		if (timeLastPellet && TIMESINCE(timeLastPellet) <= 250)
		{
			scalar beat = (TIMESINCE(timeLastPellet)/250.0f);
			scalar a = (beat < 0.5f ? ZL_Easing::InQuad(beat*2) : ZL_Easing::OutQuad(2-beat*2))*0.1f;
			DrawWalls(ZLRGBA(0,0,.8,a));
			DrawPellets(a);

			if (!PPActive)
			{
				sEnemy::DrawRevealSpriteStart(a);
				sEnemy::DrawCharStart();
				for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawChar();
				sEnemy::DrawEyeStart();
				for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawEye();
				sEnemy::DrawRevealSpriteEnd();
			}

		}
		if (PPActive)
		{
			scalar a = 1;
			if      (TIMESINCE(timeLastPowerpelletStart) < 1000) a = ZL_Easing::InQuad(TIMESINCE(timeLastPowerpelletStart)/s(1000));
			else if (TIMEUNTIL(timeLastPowerpelletEnd)   < 5000) a = ZL_Easing::OutQuad(TIMEUNTIL(timeLastPowerpelletEnd) /s(5000));
			sEnemy::DrawRevealLightStart();
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawRevealLight(a);
			sEnemy::DrawRevealLightEnd();
			sEnemy::DrawRevealSpriteStart(a);
			sEnemy::DrawCharStart();
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawChar();
			sEnemy::DrawEyeStart();
			for (pEnemy = pEnemyBegin; pEnemy != pEnemyEnd; ++pEnemy) pEnemy->DrawEye();
			sEnemy::DrawRevealSpriteEnd();
		}

		ZL_Display::PopOrtho();

		if (istitle)
		{
			DrawTitle();
		}
		else if (zlticksDead)
		{
			DrawGameOver();
		}
		else
		{
			fnt.Draw(ZLHALFW, ZLFROMH(15), ZL_String::format("SCORE: %d", Player.score), ZL_Origin::Center);
		}

		#ifdef ZILLALOG
		//fnt.Draw(10, 50, ZL_String::format("TIME: %d - SPEED: %f - SCORE: %d - timeEnemyRespawn: %d", timeTick, Player.speed, Player.score, timeEnemyRespawn));
		#endif
	}

	void DrawTextBordered(scalar x, scalar y, const char* txt, scalar scale = 1, scalar border = 2, const ZL_Color& colfill = ZLBLACK, const ZL_Color& colborder = ZL_Color::Orange, ZL_Origin::Type origin = ZL_Origin::Center)
	{
		//const static int x[] = { ZL_Display::BLEND_SRCALPHA, ZL_Display::BLEND_INVSRCALPHA, ZL_Display::BLEND_SRCCOLOR, ZL_Display::BLEND_INVSRCCOLOR, ZL_Display::BLEND_DESTCOLOR, ZL_Display::BLEND_INVDESTCOLOR, ZL_Display::BLEND_ZERO, ZL_Display::BLEND_ONE };
		//static xf = 0, xt = 0;
		//if (ZL_Display::KeyDown[ZLK_LCTRL]) { xf = (xf+1)%(sizeof(x)/sizeof(x[0])); printf("F: %04x - T: %04x\n", x[xf], x[xt]); }
		//if (ZL_Display::KeyDown[ZLK_RCTRL]) { xt = (xt+1)%(sizeof(x)/sizeof(x[0]));printf("F: %04x - T: %04x\n", x[xf], x[xt]); }
		//ZL_Display::SetBlendFunc((ZL_Display::BlendFunc)x[xf], (ZL_Display::BlendFunc)x[xt]);
		if (border>2) ZL_Display::SetBlendFunc(ZL_Display::BLEND_SRCALPHA, ZL_Display::BLEND_ONE);
		for (int i = 0; i < 9; i++) if (i != 4) { fntTitle.Draw(x+(border*((i%3)-1)), y+8+(border*((i/3)-1)), txt, scale, scale, colborder, origin); }
		ZL_Display::ResetBlendFunc();
		fntTitle.Draw(x  , y+8  , txt, scale, scale, colfill, origin);
	}

} Pakuman;

/*------------------------------------------------------------------------------------------------------*/
#define IMCMUSIC_IMCSONG_LEN 0x14
#define IMCMUSIC_IMCSONG_ROWLENSAMPLES 4410
#define IMCMUSIC_IMCSONG_ENVLISTSIZE 14
#define IMCMUSIC_IMCSONG_ENVCOUNTERLISTSIZE 17
#define IMCMUSIC_IMCSONG_OSCLISTSIZE 24
#define IMCMUSIC_IMCSONG_EFFECTLISTSIZE 12
#define IMCMUSIC_IMCSONG_VOL 50
static unsigned int IMCMUSIC_ImcSongOrderTable[] = {
  0x010000000, 0x020000000, 0x011100000, 0x021100000, 0x022200001, 0x022200011, 0x010000002, 0x010000022,
  0x011100022, 0x022200011, 0x011100011, 0x022200033, 0x011100033, 0x011100022, 0x021200022, 0x012200011,
  0x022100011, 0x012100020, 0x011000010, 0x010000010,
};
static unsigned char IMCMUSIC_ImcSongPatternData[] = {
  0x67, 0,    0x65, 0,    0x64, 0,    0x62, 0,    0x57, 0,    0x62, 0,    0x64, 0,    0x65, 255,
  0x64, 0,    0x64, 0,    0x67, 0,    0x64, 0,    0x70, 0x69, 0,    0x64, 0,    0,    0x62, 0,
  0x70, 0,    0x70, 0,    0x67, 0,    0x67, 0x60, 0,    0x64, 0x67, 0,    0x64, 0,    0,    0,
  0x64, 0,    0x64, 0,    0x67, 0,    0x67, 0,    0x70, 0x69, 0,    0x64, 0,    0,    0x62, 0,
  0x64, 0,    0x64, 0,    0x67, 0,    0x64, 0,    0x70, 0x69, 0,    0x64, 0,    0,    0x62, 0,
  0x70, 0,    0x70, 0,    0x67, 0,    0x70, 0x6B, 0x50, 0,    0x72, 0,    0x64, 0,    0,    0,
  0,    0,    0x62, 0,    255,  0,    0x62, 0,    0,    0,    0x62, 0,    0x60, 0,    0,    0,
  0,    0,    0x59, 0,    0x60, 255,  0,    0,    0x64, 0,    0,    0,    0x65, 0,    255,  0,
  0x54, 0x60, 0,    0,    0x54, 0,    0x54, 0,    0x54, 0,    0,    0,    0x54, 0,    0,    0,
  0x55, 0x55, 0,    0x54, 0x55, 0,    0x50, 0,    0x57, 0,    0,    0,    0x50, 0,    0x50, 0,
  0x44, 255,  0x52, 255,  0x54, 255,  0x50, 0x52, 0x54, 255,  0x50, 255,  0x54, 255,  0x55, 255,
  0x54, 255,  0x57, 255,  0x52, 255,  0x50, 255,  0x57, 255,  0x54, 255,  0x50, 255,  0x50, 255,
};
static unsigned char IMCMUSIC_ImcSongPatternLookupTable[] = { 0, 3, 6, 6, 6, 6, 8, 10, };
static TImcSongEnvelope IMCMUSIC_ImcSongEnvList[] = {
  { 0, 256, 97, 8, 16, 0, true, 255, },
  { 0, 256, 379, 8, 15, 255, true, 255, },
  { 0, 256, 316, 8, 16, 255, true, 255, },
  { 0, 256, 316, 8, 16, 255, true, 255, },
  { 0, 256, 87, 2, 22, 255, true, 255, },
  { 0, 256, 523, 1, 23, 255, true, 255, },
  { 128, 256, 174, 8, 16, 16, true, 255, },
  { 0, 256, 871, 8, 16, 16, true, 255, },
  { 0, 256, 523, 8, 16, 255, true, 255, },
  { 0, 256, 87, 8, 16, 255, true, 255, },
  { 196, 256, 29, 8, 16, 255, true, 255, },
  { 0, 256, 173, 8, 16, 255, true, 255, },
  { 196, 256, 31, 8, 16, 255, true, 255, },
  { 0, 128, 1046, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCMUSIC_ImcSongEnvCounterList[] = {
 { 0, 0, 256 }, { -1, -1, 256 }, { -1, -1, 258 }, { 1, 0, 254 },
  { 2, 1, 256 }, { 3, 1, 256 }, { 4, 5, 184 }, { 5, 6, 158 },
  { 6, 6, 256 }, { 7, 6, 256 }, { 8, 6, 256 }, { 9, 7, 256 },
  { 10, 7, 256 }, { 11, 7, 256 }, { 12, 7, 256 }, { 9, 7, 256 },
  { 13, 7, 128 },
};
static TImcSongOscillator IMCMUSIC_ImcSongOscillatorList[] = {
  { 7, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 117, 1, 1 },
  { 7, 5, IMCSONGOSCTYPE_SQUARE, 0, -1, 146, 1, 2 },
  { 8, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 126, 3, 1 },
  { 6, 250, IMCSONGOSCTYPE_SQUARE, 0, -1, 127, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 0, 0, 124, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 0, 1, 178, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SQUARE, 1, -1, 166, 1, 1 },
  { 9, 0, IMCSONGOSCTYPE_SQUARE, 1, -1, 255, 5, 1 },
  { 7, 0, IMCSONGOSCTYPE_SINE, 1, -1, 170, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
  { 9, 0, IMCSONGOSCTYPE_SQUARE, 5, -1, 104, 1, 1 },
  { 9, 0, IMCSONGOSCTYPE_SQUARE, 5, -1, 96, 1, 1 },
  { 9, 3, IMCSONGOSCTYPE_SAW, 5, -1, 66, 1, 1 },
  { 9, 106, IMCSONGOSCTYPE_SAW, 5, 12, 206, 1, 1 },
  { 9, 74, IMCSONGOSCTYPE_SAW, 5, 13, 80, 1, 1 },
  { 9, 106, IMCSONGOSCTYPE_SINE, 5, 14, 174, 1, 1 },
  { 5, 15, IMCSONGOSCTYPE_SINE, 6, -1, 72, 1, 8 },
  { 8, 0, IMCSONGOSCTYPE_NOISE, 6, -1, 204, 9, 1 },
  { 5, 227, IMCSONGOSCTYPE_SINE, 6, -1, 126, 10, 1 },
  { 5, 0, IMCSONGOSCTYPE_SINE, 7, -1, 98, 11, 12 },
  { 6, 0, IMCSONGOSCTYPE_SINE, 7, -1, 98, 13, 14 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 0, 15, 16 },
};
static TImcSongEffect IMCMUSIC_ImcSongEffectList[] = {
  { 115, 0, 11774, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
  { 205, 142, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
  { 22987, 245, 1, 0, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
  { 232, 136, 1, 1, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
  { 236, 112, 1, 1, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
  { 4064, 774, 1, 5, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
  { 255, 97, 1, 5, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
  { 158, 0, 3616, 5, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
  { 131, 0, 8820, 5, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
  { 2286, 3669, 1, 6, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
  { 76, 0, 1, 6, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
  { 6350, 913, 1, 7, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
};
static unsigned char IMCMUSIC_ImcSongChannelVol[8] = {100, 100, 100, 100, 100, 20, 255, 225 };
static unsigned char IMCMUSIC_ImcSongChannelEnvCounter[8] = {0, 4, 0, 0, 0, 6, 7, 1 };
static bool IMCMUSIC_ImcSongChannelStopNote[8] = {false, true, false, false, false, true, true, true };
static TImcSongData imcDataIMCMUSIC = {
  IMCMUSIC_IMCSONG_LEN, IMCMUSIC_IMCSONG_ROWLENSAMPLES, IMCMUSIC_IMCSONG_ENVLISTSIZE, IMCMUSIC_IMCSONG_ENVCOUNTERLISTSIZE, IMCMUSIC_IMCSONG_OSCLISTSIZE, IMCMUSIC_IMCSONG_EFFECTLISTSIZE, IMCMUSIC_IMCSONG_VOL,
  IMCMUSIC_ImcSongOrderTable, IMCMUSIC_ImcSongPatternData, IMCMUSIC_ImcSongPatternLookupTable, IMCMUSIC_ImcSongEnvList, IMCMUSIC_ImcSongEnvCounterList, IMCMUSIC_ImcSongOscillatorList, IMCMUSIC_ImcSongEffectList,
  IMCMUSIC_ImcSongChannelVol, IMCMUSIC_ImcSongChannelEnvCounter, IMCMUSIC_ImcSongChannelStopNote };
ZL_SynthImcTrack imcMusic(&imcDataIMCMUSIC);
/*------------------------------------------------------------------------------------------------------*/
#define IMCPAK_IMCSONG_LEN 0x1
#define IMCPAK_IMCSONG_ROWLENSAMPLES 2594
#define IMCPAK_IMCSONG_ENVLISTSIZE 2
#define IMCPAK_IMCSONG_ENVCOUNTERLISTSIZE 4
#define IMCPAK_IMCSONG_OSCLISTSIZE 13
#define IMCPAK_IMCSONG_EFFECTLISTSIZE 1
#define IMCPAK_IMCSONG_VOL 50
unsigned int IMCPAK_ImcSongOrderTable[] = {
  0x000000001,
};
unsigned char IMCPAK_ImcSongPatternData[] = {
  0x32, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
unsigned char IMCPAK_ImcSongPatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
TImcSongEnvelope IMCPAK_ImcSongEnvList[] = {
  { 0, 256, 184, 23, 255, 255, true, 255, },
  { 0, 256, 277, 1, 23, 255, true, 255, },
};
TImcSongEnvelopeCounter IMCPAK_ImcSongEnvCounterList[] = {
 { -1, -1, 256 }, { 0, 0, 2 }, { 0, 0, 2 }, { 1, 0, 158 },
};
TImcSongOscillator IMCPAK_ImcSongOscillatorList[] = {
  { 9, 4, IMCSONGOSCTYPE_SAW, 0, -1, 0, 0, 0 },
  { 7, 31, IMCSONGOSCTYPE_SAW, 0, -1, 255, 0, 0 },
  { 8, 36, IMCSONGOSCTYPE_SAW, 0, -1, 255, 0, 0 },
  { 6, 0, IMCSONGOSCTYPE_SINE, 0, -1, 0, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_NOISE, 0, 1, 126, 1, 0 },
  { 8, 0, IMCSONGOSCTYPE_NOISE, 0, 2, 136, 2, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
TImcSongEffect IMCPAK_ImcSongEffectList[] = {
  { 236, 134, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 3, 0 },
};
unsigned char IMCPAK_ImcSongChannelVol[8] = {171, 100, 100, 100, 100, 100, 100, 100 };
unsigned char IMCPAK_ImcSongChannelEnvCounter[8] = {0, 0, 0, 0, 0, 0, 0, 0 };
bool IMCPAK_ImcSongChannelStopNote[8] = {true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCPAK = {
  IMCPAK_IMCSONG_LEN, IMCPAK_IMCSONG_ROWLENSAMPLES, IMCPAK_IMCSONG_ENVLISTSIZE, IMCPAK_IMCSONG_ENVCOUNTERLISTSIZE, IMCPAK_IMCSONG_OSCLISTSIZE, IMCPAK_IMCSONG_EFFECTLISTSIZE, IMCPAK_IMCSONG_VOL,
  IMCPAK_ImcSongOrderTable, IMCPAK_ImcSongPatternData, IMCPAK_ImcSongPatternLookupTable, IMCPAK_ImcSongEnvList, IMCPAK_ImcSongEnvCounterList, IMCPAK_ImcSongOscillatorList, IMCPAK_ImcSongEffectList,
  IMCPAK_ImcSongChannelVol, IMCPAK_ImcSongChannelEnvCounter, IMCPAK_ImcSongChannelStopNote };
/*------------------------------------------------------------------------------------------------------*/
  #define IMCPOWER_IMCSONG_LEN 0x1
#define IMCPOWER_IMCSONG_ROWLENSAMPLES 2594
#define IMCPOWER_IMCSONG_ENVLISTSIZE 2
#define IMCPOWER_IMCSONG_ENVCOUNTERLISTSIZE 3
#define IMCPOWER_IMCSONG_OSCLISTSIZE 12
#define IMCPOWER_IMCSONG_EFFECTLISTSIZE 5
#define IMCPOWER_IMCSONG_VOL 70
unsigned int IMCPOWER_ImcSongOrderTable[] = {
  0x000000011,
};
unsigned char IMCPOWER_ImcSongPatternData[] = {
  0x60, 0x62, 0x64, 0x65, 0x67, 0,    0,    0,    255,  0,    0,    0,    0,    0,    0,    0,
  0x20, 0x22, 0x24, 0x25, 0,    0,    255,  0,    0,    0,    0,    0,    0,    0,    0,    0,
};
unsigned char IMCPOWER_ImcSongPatternLookupTable[] = { 0, 1, 2, 2, 2, 2, 2, 2, };
TImcSongEnvelope IMCPOWER_ImcSongEnvList[] = {
  { 0, 256, 65, 8, 16, 255, true, 255, },
  { 0, 256, 361, 8, 16, 0, true, 255, },
};
TImcSongEnvelopeCounter IMCPOWER_ImcSongEnvCounterList[] = {
 { 0, 0, 256 }, { -1, -1, 256 }, { 1, 1, 256 },
};
TImcSongOscillator IMCPOWER_ImcSongOscillatorList[] = {
  { 9, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 98, 1, 1 },
  { 8, 127, IMCSONGOSCTYPE_SQUARE, 0, 0, 50, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 20, 1, 1 },
  { 9, 0, IMCSONGOSCTYPE_SINE, 1, -1, 52, 1, 1 },
  { 8, 127, IMCSONGOSCTYPE_SINE, 1, -1, 98, 1, 1 },
  { 7, 0, IMCSONGOSCTYPE_SINE, 1, -1, 194, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
TImcSongEffect IMCPOWER_ImcSongEffectList[] = {
  { 148, 0, 5188, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
  { 60, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
  { 50, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
  { 254, 32896, 1, 1, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
  { 23, 0, 1, 1, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
};
unsigned char IMCPOWER_ImcSongChannelVol[8] = {197, 15, 100, 100, 100, 100, 100, 100 };
unsigned char IMCPOWER_ImcSongChannelEnvCounter[8] = {0, 2, 0, 0, 0, 0, 0, 0 };
bool IMCPOWER_ImcSongChannelStopNote[8] = {true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCPOWER = {
  IMCPOWER_IMCSONG_LEN, IMCPOWER_IMCSONG_ROWLENSAMPLES, IMCPOWER_IMCSONG_ENVLISTSIZE, IMCPOWER_IMCSONG_ENVCOUNTERLISTSIZE, IMCPOWER_IMCSONG_OSCLISTSIZE, IMCPOWER_IMCSONG_EFFECTLISTSIZE, IMCPOWER_IMCSONG_VOL,
  IMCPOWER_ImcSongOrderTable, IMCPOWER_ImcSongPatternData, IMCPOWER_ImcSongPatternLookupTable, IMCPOWER_ImcSongEnvList, IMCPOWER_ImcSongEnvCounterList, IMCPOWER_ImcSongOscillatorList, IMCPOWER_ImcSongEffectList,
  IMCPOWER_ImcSongChannelVol, IMCPOWER_ImcSongChannelEnvCounter, IMCPOWER_ImcSongChannelStopNote };
ZL_SynthImcTrack imcPower(&imcDataIMCPOWER, false);
/*------------------------------------------------------------------------------------------------------*/
#define IMCEAT_IMCSONG_LEN 0x1
#define IMCEAT_IMCSONG_ROWLENSAMPLES 5512
#define IMCEAT_IMCSONG_ENVLISTSIZE 3
#define IMCEAT_IMCSONG_ENVCOUNTERLISTSIZE 4
#define IMCEAT_IMCSONG_OSCLISTSIZE 12
#define IMCEAT_IMCSONG_EFFECTLISTSIZE 3
#define IMCEAT_IMCSONG_VOL 200
unsigned int IMCEAT_ImcSongOrderTable[] = {
  0x000000011,
};
unsigned char IMCEAT_ImcSongPatternData[] = {
  0x40, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  0x40, 0x42, 0x44, 255,  0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
unsigned char IMCEAT_ImcSongPatternLookupTable[] = { 0, 1, 2, 2, 2, 2, 2, 2, };
TImcSongEnvelope IMCEAT_ImcSongEnvList[] = {
  { 0, 256, 31, 4, 20, 255, true, 255, },
  { 128, 256, 173, 8, 32, 255, true, 255, },
  { 0, 256, 64, 8, 16, 255, true, 255, },
};
TImcSongEnvelopeCounter IMCEAT_ImcSongEnvCounterList[] = {
 { 0, 0, 224 }, { -1, -1, 256 }, { 1, 0, 256 }, { 2, 1, 256 },
};
TImcSongOscillator IMCEAT_ImcSongOscillatorList[] = {
  { 8, 0, IMCSONGOSCTYPE_SAW, 0, -1, 100, 1, 2 },
  { 8, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 30, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 18, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 0, 1, 100, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 255, 1, 1 },
  { 10, 0, IMCSONGOSCTYPE_SINE, 1, 4, 255, 1, 1 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
TImcSongEffect IMCEAT_ImcSongEffectList[] = {
  { 56, 179, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
  { 117, 0, 11024, 1, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
  { 255, 0, 1, 1, IMCSONGEFFECTTYPE_HIGHPASS, 1, 0 },
};
unsigned char IMCEAT_ImcSongChannelVol[8] = {50, 100, 100, 100, 100, 100, 100, 100 };
unsigned char IMCEAT_ImcSongChannelEnvCounter[8] = {0, 3, 0, 0, 0, 0, 0, 0 };
bool IMCEAT_ImcSongChannelStopNote[8] = {true, true, false, false, false, false, false, false };
TImcSongData imcDataIMCEAT = {
  IMCEAT_IMCSONG_LEN, IMCEAT_IMCSONG_ROWLENSAMPLES, IMCEAT_IMCSONG_ENVLISTSIZE, IMCEAT_IMCSONG_ENVCOUNTERLISTSIZE, IMCEAT_IMCSONG_OSCLISTSIZE, IMCEAT_IMCSONG_EFFECTLISTSIZE, IMCEAT_IMCSONG_VOL,
  IMCEAT_ImcSongOrderTable, IMCEAT_ImcSongPatternData, IMCEAT_ImcSongPatternLookupTable, IMCEAT_ImcSongEnvList, IMCEAT_ImcSongEnvCounterList, IMCEAT_ImcSongOscillatorList, IMCEAT_ImcSongEffectList,
  IMCEAT_ImcSongChannelVol, IMCEAT_ImcSongChannelEnvCounter, IMCEAT_ImcSongChannelStopNote };
ZL_SynthImcTrack imcEat(&imcDataIMCEAT, false);
/*------------------------------------------------------------------------------------------------------*/
#define IMCDEAD_IMCSONG_LEN 0x1
#define IMCDEAD_IMCSONG_ROWLENSAMPLES 5512
#define IMCDEAD_IMCSONG_ENVLISTSIZE 7
#define IMCDEAD_IMCSONG_ENVCOUNTERLISTSIZE 8
#define IMCDEAD_IMCSONG_OSCLISTSIZE 12
#define IMCDEAD_IMCSONG_EFFECTLISTSIZE 4
#define IMCDEAD_IMCSONG_VOL 150
unsigned int IMCDEAD_ImcSongOrderTable[] = {
  0x000000011,
};
unsigned char IMCDEAD_ImcSongPatternData[] = {
  0x50, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  0x2B, 255,  0x27, 255,  0x24, 255,  0x22, 0x22, 0x22, 255,  0,    0,    0,    0,    0,    0,
};
unsigned char IMCDEAD_ImcSongPatternLookupTable[] = { 0, 1, 2, 2, 2, 2, 2, 2, };
TImcSongEnvelope IMCDEAD_ImcSongEnvList[] = {
  { 0, 256, 65, 8, 16, 4, true, 255, },
  { 0, 256, 13, 8, 16, 255, true, 255, },
  { 0, 256, 30, 0, 24, 255, true, 255, },
  { 0, 100, 523, 8, 255, 255, true, 255, },
  { 0, 256, 2092, 8, 255, 255, true, 255, },
  { 0, 256, 434, 8, 16, 0, true, 255, },
  { 0, 256, 130, 8, 16, 255, true, 255, },
};
TImcSongEnvelopeCounter IMCDEAD_ImcSongEnvCounterList[] = {
 { 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 128 }, { 3, 0, 100 },
  { -1, -1, 256 }, { 4, 0, 256 }, { 5, 1, 256 }, { 6, 1, 256 },
};
TImcSongOscillator IMCDEAD_ImcSongOscillatorList[] = {
  { 9, 66, IMCSONGOSCTYPE_SINE, 0, -1, 68, 1, 2 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 0, 0, 255, 3, 4 },
  { 8, 2, IMCSONGOSCTYPE_SAW, 1, -1, 122, 4, 4 },
  { 7, 0, IMCSONGOSCTYPE_SAW, 1, -1, 255, 4, 4 },
  { 6, 0, IMCSONGOSCTYPE_SINE, 1, -1, 68, 4, 4 },
  { 8, 0, IMCSONGOSCTYPE_SQUARE, 1, 2, 26, 4, 4 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
  { 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
TImcSongEffect IMCDEAD_ImcSongEffectList[] = {
  { 134, 220, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 5, 4 },
  { 7, 0, 1, 0, IMCSONGEFFECTTYPE_HIGHPASS, 4, 0 },
  { 2921, 1671, 1, 1, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 4 },
  { 236, 156, 1, 1, IMCSONGEFFECTTYPE_RESONANCE, 7, 4 },
};
unsigned char IMCDEAD_ImcSongChannelVol[8] = {255, 17, 100, 100, 100, 100, 100, 100 };
unsigned char IMCDEAD_ImcSongChannelEnvCounter[8] = {0, 6, 0, 0, 0, 0, 0, 0 };
bool IMCDEAD_ImcSongChannelStopNote[8] = {false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCDEAD = {
  IMCDEAD_IMCSONG_LEN, IMCDEAD_IMCSONG_ROWLENSAMPLES, IMCDEAD_IMCSONG_ENVLISTSIZE, IMCDEAD_IMCSONG_ENVCOUNTERLISTSIZE, IMCDEAD_IMCSONG_OSCLISTSIZE, IMCDEAD_IMCSONG_EFFECTLISTSIZE, IMCDEAD_IMCSONG_VOL,
  IMCDEAD_ImcSongOrderTable, IMCDEAD_ImcSongPatternData, IMCDEAD_ImcSongPatternLookupTable, IMCDEAD_ImcSongEnvList, IMCDEAD_ImcSongEnvCounterList, IMCDEAD_ImcSongOscillatorList, IMCDEAD_ImcSongEffectList,
  IMCDEAD_ImcSongChannelVol, IMCDEAD_ImcSongChannelEnvCounter, IMCDEAD_ImcSongChannelStopNote };
ZL_SynthImcTrack imcDead(&imcDataIMCDEAD, false);
/*------------------------------------------------------------------------------------------------------*/
