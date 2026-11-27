// ============================================================
// 纪念碑小谷 · Monument Valley 风格等距解谜 Demo (视觉升级版)
// 纯 C++ + Win32 + GDI+(抗锯齿/渐变), 无第三方依赖
//
// 编译 (MSVC x64 命令行):
//   cl /EHsc /O2 /utf-8 monument.cpp /Fe:MonumentDemo.exe ^
//      /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib
//
// 玩法:
//   鼠标左键点击青色平台 -> 旋转 90°; 点击金色滑块 -> 沿轨道平移一格
//   小人站上机关会被一起带走(乘坐)
//   方向键 / WASD -> 移动小人, 走到金色穹顶处通关
//   视错觉核心: 两块砖只要"画面上相邻"就能走过去 —— 哪怕现实里隔着悬空!
//   R 重开本关   N 通关后下一关   数字键 1-0 跳关   Esc 退出
// ============================================================
#include <windows.h>
#include <gdiplus.h>
#include <array>
#include <cmath>
#include <cwchar>
#include <vector>

using namespace Gdiplus;

static const float PI = 3.14159265f;
static const int   CW = 32;      // 菱形半宽
static const int   CHH = 16;     // 菱形半高
static const int   HSTEP = 16;   // 每单位高度的像素(必须=CHH, 视错觉对齐的前提)
static const int   SIDE = 26;    // 浮岛基座厚度
static const int   OX = 460, OY = 185;
static const int   WINW = 960, WINH = 640;

// ---------------- 关卡数据 ----------------
struct Off { int dx, dy; };
struct Rot { int px, py, k, h; std::vector<Off> offs; };
// 滑块: 基点(bx,by,bh) 沿方向 d 每次点击平移一格, 档位 t ∈ [0, steps]
struct Sli { int bx, by, bh; int dx, dy, dh; int steps; std::vector<Off> offs; };
struct Lev {
    int w, h;
    const wchar_t* name;
    std::vector<std::array<int, 3>> stat;
    int sx, sy, gx, gy;
    std::vector<Rot> rots;
    std::vector<Sli> slis;
};

static Lev g_levels[10];
static int  g_cur = 0;
static int  g_w, g_h, g_gx, g_gy;
static const wchar_t* g_name = L"";
static std::vector<std::array<int, 3>> g_stat;
static std::vector<Rot> g_rots;
static std::vector<Sli> g_slis;
static std::vector<int> g_sliT;          // 每个滑块当前档位
struct Cell { int x, y, h; };
static std::vector<Cell> g_occ;

// ---------------- 游戏状态 ----------------
static const int ST_PLAY = 0, ST_ROT = 1, ST_WALK = 2, ST_WIN = 3, ST_SLI = 4;
static int g_state = ST_PLAY;
static int  g_rotGrp = -1;
static float g_rotT = 0.f;
static bool  g_ride = false;
static int   g_rdx, g_rdy;
static float g_rideH = 1.f;
static int   g_sliGrp = -1;
static float g_sliAnim = 0.f;
static bool  g_sRide = false;
static int   g_srox, g_sroy, g_sroh;     // 乘坐时小人相对滑块基点的偏移
static int   g_ccx, g_ccy;
static float g_chf = 1.f;
static int   g_wfx, g_wfy, g_wfh;
static int   g_wtx, g_wty, g_wth;
static float g_walkT = 0.f;

static HFONT g_fontBig, g_fontTitle, g_fontSmall;

static Off rot1(Off o) { Off r; r.dx = o.dy; r.dy = -o.dx; return r; }
static Off rotN(Off o, int k) { for (int i = 0; i < k; ++i) o = rot1(o); return o; }
static float ease(float t) { return t * t * (3.f - 2.f * t); }   // 平滑缓动

static void buildOcc() {
    g_occ.clear();
    for (auto& s : g_stat) g_occ.push_back({ s[0], s[1], s[2] });
    for (auto& r : g_rots) {
        g_occ.push_back({ r.px, r.py, r.h });
        for (auto& o : r.offs) {
            Off a = rotN(o, r.k);
            g_occ.push_back({ r.px + a.dx, r.py + a.dy, r.h });
        }
    }
    for (size_t si = 0; si < g_slis.size(); ++si) {
        const Sli& s = g_slis[si];
        int t = g_sliT[si];
        g_occ.push_back({ s.bx + t * s.dx, s.by + t * s.dy, s.bh + t * s.dh });
        for (auto& o : s.offs)
            g_occ.push_back({ s.bx + t * s.dx + o.dx, s.by + t * s.dy + o.dy, s.bh + t * s.dh });
    }
}
static int tileH(int x, int y) {
    for (auto& c : g_occ) if (c.x == x && c.y == y) return c.h;
    return -1;
}
static bool charOnGroup(int gi) {
    const Rot& r = g_rots[gi];
    if (g_ccx == r.px && g_ccy == r.py) return true;
    for (auto& o : r.offs) { Off a = rotN(o, r.k); if (g_ccx == r.px + a.dx && g_ccy == r.py + a.dy) return true; }
    return false;
}
static bool charOnSli(int si) {
    const Sli& s = g_slis[si];
    int t = g_sliT[si];
    if (g_ccx == s.bx + t * s.dx && g_ccy == s.by + t * s.dy) return true;
    for (auto& o : s.offs)
        if (g_ccx == s.bx + t * s.dx + o.dx && g_ccy == s.by + t * s.dy + o.dy) return true;
    return false;
}

static void loadLevel(int idx) {
    g_cur = idx;                            // 同步当前关(编号+配色)
    const Lev& L = g_levels[idx];
    g_w = L.w; g_h = L.h; g_gx = L.gx; g_gy = L.gy;
    g_name = L.name;
    g_stat = L.stat; g_rots = L.rots;
    g_slis = L.slis;
    g_sliT.assign(g_slis.size(), 0);
    g_ccx = L.sx; g_ccy = L.sy;
    for (auto& s : g_stat) if (s[0] == L.sx && s[1] == L.sy) g_chf = (float)s[2];
    buildOcc();
    g_state = ST_PLAY;
    g_rotGrp = -1; g_sliGrp = -1;
    g_ride = false; g_sRide = false;
}

static void initLevels() {
    {   // 1 桥: 教学关
        Lev L; L.w = 6; L.h = 5; L.name = L"桥";
        L.stat = { {0,2,1},{1,2,1},{5,2,2} };
        L.sx = 0; L.sy = 2; L.gx = 5; L.gy = 2;
        Rot r; r.px = 3; r.py = 2; r.k = 1; r.h = 1;
        r.offs = { {1,0},{-1,0} };
        L.rots = { r };
        g_levels[0] = L;
    }
    {   // 2 双桥
        Lev L; L.w = 7; L.h = 6; L.name = L"双桥";
        L.stat = { {0,3,1},{1,3,1},{3,3,2},{5,3,2},{6,3,2} };
        L.sx = 0; L.sy = 3; L.gx = 6; L.gy = 3;
        Rot a; a.px = 2; a.py = 2; a.k = 3; a.h = 1; a.offs = { {0,1} };
        Rot b; b.px = 4; b.py = 4; b.k = 3; b.h = 2; b.offs = { {0,-1} };
        L.rots = { a, b };
        g_levels[1] = L;
    }
    {   // 3 折返
        Lev L; L.w = 7; L.h = 5; L.name = L"折返";
        L.stat = { {0,2,1},{1,2,1},{3,2,1},{5,2,2},{5,3,2},{5,4,2},{6,4,2} };
        L.sx = 0; L.sy = 2; L.gx = 6; L.gy = 4;
        Rot c; c.px = 2; c.py = 3; c.k = 2; c.h = 1; c.offs = { {0,-1} };
        Rot d; d.px = 4; d.py = 1; d.k = 3; d.h = 1; d.offs = { {0,1} };
        L.rots = { c, d };
        g_levels[2] = L;
    }
    {   // 4 蛇梯
        Lev L; L.w = 7; L.h = 7; L.name = L"蛇梯";
        L.stat = { {3,6,1},{3,5,1},{3,3,1},{3,1,2},{2,1,2},{1,1,2} };
        L.sx = 3; L.sy = 6; L.gx = 1; L.gy = 1;
        Rot a; a.px = 2; a.py = 4; a.k = 2; a.h = 1; a.offs = { {1,0} };
        Rot b; b.px = 4; b.py = 2; b.k = 3; b.h = 1; b.offs = { {-1,0} };
        L.rots = { a, b };
        g_levels[3] = L;
    }
    {   // 5 电梯: 垂直滑块 + 视错觉跨空登塔
        Lev L; L.w = 8; L.h = 6; L.name = L"电梯";
        L.stat = { {0,3,1},{1,3,1},{4,4,7},{5,4,7},{6,4,7} };
        L.sx = 0; L.sy = 3; L.gx = 6; L.gy = 4;
        Sli e; e.bx = 2; e.by = 3; e.bh = 1; e.dx = 0; e.dy = 0; e.dh = 1; e.steps = 2;
        L.slis = { e };
        g_levels[4] = L;
    }
    {   // 6 滑桥: 水平滑块, 站上去被一起带走
        Lev L; L.w = 7; L.h = 6; L.name = L"滑桥";
        L.stat = { {0,4,1},{1,4,1},{5,4,2},{6,4,2} };
        L.sx = 0; L.sy = 4; L.gx = 6; L.gy = 4;
        Sli s; s.bx = 2; s.by = 4; s.bh = 1; s.dx = 1; s.dy = 0; s.dh = 0; s.steps = 2;
        s.offs = { {1,0} };
        L.slis = { s };
        g_levels[5] = L;
    }
    {   // 7 错觉阶: 纯视错觉登山 + 旋转平台收尾
        Lev L; L.w = 10; L.h = 7; L.name = L"错觉阶";
        L.stat = { {0,4,1},{1,4,1},{2,4,3},{4,5,5},{5,5,5},{8,5,5},{9,5,5} };
        L.sx = 0; L.sy = 4; L.gx = 9; L.gy = 5;
        Rot r; r.px = 7; r.py = 5; r.k = 0; r.h = 5; r.offs = { {1,0} };
        L.rots = { r };
        g_levels[6] = L;
    }
    {   // 8 双滑: 滑桥接电梯, 顶部错觉登塔
        Lev L; L.w = 9; L.h = 8; L.name = L"双滑";
        L.stat = { {0,6,1},{1,6,1},{7,7,7},{8,7,7} };
        L.sx = 0; L.sy = 6; L.gx = 8; L.gy = 7;
        Sli a; a.bx = 2; a.by = 6; a.bh = 1; a.dx = 1; a.dy = 0; a.dh = 0; a.steps = 1;
        a.offs = { {1,0} };
        Sli b; b.bx = 5; b.by = 6; b.bh = 1; b.dx = 0; b.dy = 0; b.dh = 1; b.steps = 2;
        L.slis = { a, b };
        g_levels[7] = L;
    }
    {   // 9 塔梯: 旋转平台 + 升降梯 + 错觉
        Lev L; L.w = 9; L.h = 5; L.name = L"塔梯";
        L.stat = { {0,2,1},{1,2,1},{4,2,2},{7,3,7},{8,3,7} };
        L.sx = 0; L.sy = 2; L.gx = 8; L.gy = 3;
        Rot r; r.px = 3; r.py = 2; r.k = 1; r.h = 1; r.offs = { {1,0} };
        Sli e; e.bx = 5; e.by = 2; e.bh = 2; e.dx = 0; e.dy = 0; e.dh = 1; e.steps = 3;
        L.rots = { r };
        L.slis = { e };
        g_levels[8] = L;
    }
    {   // 10 终章: 旋转 + 滑桥 + 升降梯
        Lev L; L.w = 10; L.h = 8; L.name = L"终章";
        L.stat = { {0,6,1},{1,6,1},{4,6,2},{8,6,6},{9,6,6} };
        L.sx = 0; L.sy = 6; L.gx = 9; L.gy = 6;
        Rot r; r.px = 2; r.py = 6; r.k = 3; r.h = 1; r.offs = { {1,0} };
        Sli a; a.bx = 5; a.by = 6; a.bh = 2; a.dx = 1; a.dy = 0; a.dh = 0; a.steps = 1;
        Sli b; b.bx = 7; b.by = 6; b.bh = 3; b.dx = 0; b.dy = 0; b.dh = 1; b.steps = 2;
        L.rots = { r };
        L.slis = { a, b };
        g_levels[9] = L;
    }
}

// 视错觉核心: 目标砖只要在屏幕上与小人"看起来相差一步"就能走过去
// 屏幕步长 = (±CW, ±CHH)。网格上可能隔着几格、高度差好几个单位 —— 画面接上了就是路
static void startWalk(int tx, int ty, int th) {
    g_wfx = g_ccx; g_wfy = g_ccy; g_wfh = tileH(g_ccx, g_ccy);
    g_wtx = tx; g_wty = ty; g_wth = th;
    g_walkT = 0.f;
    g_state = ST_WALK;
}
// 移动判定:
//  1) 普通相邻: 网格一步且高度差 <=1 (含"向上走一格"的台阶, 其屏幕位移是 (±32,0))
//  2) 视错觉: 按键方向的屏幕步长 (±32,±16) 正好有块对得上 —— 哪怕网格上隔着几格/悬空
static void tryMove(int dx, int dy) {
    if (g_state != ST_PLAY) return;
    int ch = tileH(g_ccx, g_ccy);
    int nx = g_ccx + dx, ny = g_ccy + dy;
    int nh = tileH(nx, ny);
    if (nh >= 0 && abs(nh - ch) <= 1) { startWalk(nx, ny, nh); return; }
    int sdx = (dx - dy) * CW, sdy = (dx + dy) * CHH;
    for (auto& c : g_occ) {
        if (c.x == g_ccx && c.y == g_ccy) continue;
        int ddx = c.x - g_ccx, ddy = c.y - g_ccy, ddh = c.h - ch;
        if ((ddx - ddy) * CW != sdx) continue;
        if ((ddx + ddy) * CHH - ddh * HSTEP != sdy) continue;
        startWalk(c.x, c.y, c.h);
        return;
    }
}

static void continueHeldKeys() {
    if (GetAsyncKeyState(VK_UP) & 0x8000) tryMove(0, -1);
    else if (GetAsyncKeyState(VK_DOWN) & 0x8000) tryMove(0, 1);
    else if (GetAsyncKeyState(VK_LEFT) & 0x8000) tryMove(-1, 0);
    else if (GetAsyncKeyState(VK_RIGHT) & 0x8000) tryMove(1, 0);
}

// ============================================================
//                        GDI+ 渲染
// ============================================================
static Color Shade(Color c, float f) {
    BYTE r = (BYTE)min(255.f, c.GetR() * f);
    BYTE g = (BYTE)min(255.f, c.GetG() * f);
    BYTE b = (BYTE)min(255.f, c.GetB() * f);
    return Color(c.GetA(), r, g, b);
}
static Color Mix(Color a, Color b, float t) {
    BYTE r = (BYTE)min(255.f, a.GetR() + (b.GetR() - a.GetR()) * t);
    BYTE g = (BYTE)min(255.f, a.GetG() + (b.GetG() - a.GetG()) * t);
    BYTE b2 = (BYTE)min(255.f, a.GetB() + (b.GetB() - a.GetB()) * t);
    return Color(255, r, g, b2);
}

// 每关独立配色(致敬原版每章一个色调)
struct Pal {
    Color skyTop, skyBot, hillFar, hillNear;
    Color top, sideL, sideR, edge;          // 静态块
    Color rotTop, rotL, rotR, rotEdge;      // 可旋转平台
    Color pivot;
};
static Pal g_pals[10];
static Pal g_pal;

static void initPalettes() {
    // 参数: skyTop skyBot | top sideL sideR | rotTop rotL rotR | edge rotEdge
    auto P = [](Pal& p, int skyR, int skyG, int skyB, int botR, int botG, int botB,
                int tR, int tG, int tB, int lR, int lG, int lB, int rR, int rG, int rB,
                int rtR, int rtG, int rtB, int rlR, int rlG, int rlB, int rrR, int rrG, int rrB,
                int eR, int eG, int eB, int reR, int reG, int reB) {
        p.skyTop = Color(255, skyR, skyG, skyB);
        p.skyBot = Color(255, botR, botG, botB);
        p.hillFar = Color(46, botR, botG, botB);
        p.hillNear = Color(34, (botR * 3 + tR) / 4, (botG * 3 + tG) / 4, (botB * 3 + tB) / 4);
        p.top = Color(255, tR, tG, tB);
        p.sideL = Color(255, lR, lG, lB);
        p.sideR = Color(255, rR, rG, rB);
        p.edge = Color(215, eR, eG, eB);
        p.rotTop = Color(255, rtR, rtG, rtB);
        p.rotL = Color(255, rlR, rlG, rlB);
        p.rotR = Color(255, rrR, rrG, rrB);
        p.rotEdge = Color(235, reR, reG, reB);
        p.pivot = p.rotEdge;
    };
    P(g_pals[0], 247,243,234, 239,230,216,  240,238,251, 205,201,242, 176,168,232,  200,239,226, 156,220,196, 114,199,168,  106,95,184,  46,143,107);   // 桥 经典淡紫
    P(g_pals[1], 239,247,244, 226,239,233,  231,243,238, 191,226,211, 147,203,180,  251,238,214, 244,216,159, 233,188,108,  62,126,104,  168,116,31);   // 双桥 青瓷
    P(g_pals[2], 251,241,236, 245,226,218,  249,227,224, 240,183,182, 230,149,151,  220,235,244, 175,206,228, 130,180,217,  168,91,96,   60,110,150);   // 折返 珊瑚
    P(g_pals[3], 250,242,223, 243,226,194,  246,227,184, 232,199,126, 217,174,86,   227,239,246, 188,219,236, 143,190,220,  156,116,32,  61,110,146);   // 蛇梯 沙金
    P(g_pals[4], 237,245,250, 220,234,244,  221,235,244, 172,206,229, 127,169,208,  251,233,220, 243,200,172, 234,165,119,  58,110,150,  165,90,42);    // 转盘 天青
    P(g_pals[5], 250,240,244, 241,221,230,  247,223,231, 235,176,196, 220,138,166,  225,240,223, 185,220,180, 143,196,135,  150,83,110,  78,127,69);    // 同心转 蔷薇
    P(g_pals[6], 230,228,242, 211,205,230,  228,225,242, 194,188,230, 162,154,224,  244,231,210, 233,203,156, 219,176,105,  90,80,153,   147,101,28);   // 迷阵 靛夜
    P(g_pals[7], 253,242,231, 248,227,208,  251,233,216, 245,201,164, 236,169,116,  223,240,234, 176,223,210, 130,201,180,  168,100,40,  53,128,107);   // 回旋木马 蜜桃
    P(g_pals[8], 251,233,211, 245,207,174,  248,236,212, 241,212,158, 231,188,108,  230,224,244, 195,186,232, 158,146,216,  166,122,40,  92,80,160);    // 长征 落日
    P(g_pals[9], 227,221,241, 201,191,223,  237,232,248, 203,194,236, 172,159,224,  216,240,230, 165,223,201, 120,201,171,  100,90,158,  46,138,104);   // 终章 暮紫
}

static void topCenter(float fx, float fy, float h, float& sx, float& sy) {
    sx = OX + (fx - fy) * CW;
    sy = OY + (fx + fy + 1) * CHH - h * HSTEP;
}

static void fillDiamond(Graphics& g, float sx, float sy, Brush& br, Pen& pen) {
    PointF pts[4] = { PointF(sx, sy - CHH), PointF(sx + CW, sy), PointF(sx, sy + CHH), PointF(sx - CW, sy) };
    g.FillPolygon(&br, pts, 4);
    g.DrawPolygon(&pen, pts, 4);
}

// kind: 0=静态块 1=可旋转平台 2=终点 3=滑块(金色)
static void drawTile(Graphics& g, float fx, float fy, float h, int kind) {
    float sx, sy; topCenter(fx, fy, h, sx, sy);
    float D = h * HSTEP + SIDE;
    bool rot = (kind == 1);
    Color cT = rot ? g_pal.rotTop : g_pal.top;
    Color cL = rot ? g_pal.rotL : g_pal.sideL;
    Color cR = rot ? g_pal.rotR : g_pal.sideR;
    Color cE = rot ? g_pal.rotEdge : g_pal.edge;
    if (kind == 3) {   // 滑块: 暖金琥珀, 与青色旋转平台区分
        cT = Color(255, 252, 214, 138);
        cL = Color(255, 236, 186, 100);
        cR = Color(255, 212, 156, 66);
        cE = Color(235, 164, 106, 22);
    } else if (kind == 0 || kind == 2) {   // 石砖材质: 参考正版灰绿砂岩, 少量关卡色调
        cT = Mix(cT, Color(255, 170, 178, 168), 0.52f);
        cL = Mix(cL, Color(255, 197, 205, 197), 0.52f);
        cR = Mix(cR, Color(255, 177, 187, 179), 0.52f);
        cE = Mix(cE, Color(255, 106, 116, 110), 0.45f);
    }

    // 左右侧面: 垂直渐变(底部更暗), 营造体积感
    {
        LinearGradientBrush lb(PointF(sx - CW, sy + CHH), PointF(sx - CW, sy + CHH + D),
            cL, Shade(Color(255, cL.GetR(), cL.GetG(), cL.GetB()), 0.62f));
        PointF pts[4] = { PointF(sx - CW, sy), PointF(sx, sy + CHH), PointF(sx, sy + CHH + D), PointF(sx - CW, sy + D) };
        g.FillPolygon(&lb, pts, 4);
    }
    {
        LinearGradientBrush lb(PointF(sx, sy + CHH), PointF(sx, sy + CHH + D),
            cR, Shade(Color(255, cR.GetR(), cR.GetG(), cR.GetB()), 0.62f));
        PointF pts[4] = { PointF(sx, sy + CHH), PointF(sx + CW, sy), PointF(sx + CW, sy + D), PointF(sx, sy + CHH + D) };
        g.FillPolygon(&lb, pts, 4);
    }
    // 石砖侧面横缝线(沿菱形斜率平行)
    if (kind == 0 || kind == 2) {
        Pen seam(Color(60, 40, 52, 50), 1.f);
        float off = D * 0.45f;
        g.DrawLine(&seam, sx - CW, sy + off, sx, sy + CHH + off);
        g.DrawLine(&seam, sx, sy + CHH + off, sx + CW, sy + off);
    }
    // 顶面: 由远及近轻微明暗渐变
    {
        LinearGradientBrush lb(PointF(sx, sy - CHH), PointF(sx, sy + CHH),
            Shade(cT, 1.10f), cT);
        fillDiamond(g, sx, sy, lb, Pen(cE, 1.0f));
    }
    // 石砖顶面同心圆雕纹(正版石砖质感)
    if (kind == 0 || kind == 2) {
        g.DrawEllipse(&Pen(Color(80, 255, 255, 255), 1.2f), sx - 10.f, sy - 5.f, 20.f, 10.f);
        g.DrawEllipse(&Pen(Color(60, 30, 42, 40), 1.0f), sx - 5.5f, sy - 2.75f, 11.f, 5.5f);
        SolidBrush cd(Color(95, 255, 255, 255));
        g.FillEllipse(&cd, sx - 1.5f, sy - 0.75f, 3.f, 1.5f);
    }
    // 顶面内圈高光(机关专属, 机械感)
    if (rot || kind == 3) {
        SolidBrush hb(Color(70, 255, 255, 255));
        PointF pts[4] = { PointF(sx, sy - CHH * 0.52f), PointF(sx + CW * 0.52f, sy),
                          PointF(sx, sy + CHH * 0.52f), PointF(sx - CW * 0.52f, sy) };
        g.FillPolygon(&hb, pts, 4);
        g.DrawPolygon(&Pen(Color(90, 255, 255, 255), 1.0f), pts, 4);
    }
    // 基座底缘描边(悬浮岛收口)
    PointF btm[4] = { PointF(sx - CW, sy + D - 1), PointF(sx, sy + CHH + D - 1),
                      PointF(sx + CW, sy + D - 1), PointF(sx, sy + CHH + D + 4) };
    g.DrawLines(&Pen(Color(140, cE.GetR(), cE.GetG(), cE.GetB()), 1.2f), btm, 3);
}

// 终点: 发光金色穹顶 (原版的圣几何意象)
static void drawGoal(Graphics& g) {
    float sx, sy; topCenter((float)g_gx, (float)g_gy, tileH(g_gx, g_gy), sx, sy);
    // 柔光
    {
        GraphicsPath p; p.AddEllipse(sx - 55.f, sy - 62.f, 110.f, 110.f);
        PathGradientBrush pb(&p);
        pb.SetCenterPoint(PointF(sx, sy - 7));
        Color cs[1] = { Color(120, 255, 214, 120) };
        pb.SetCenterColor(cs[0]);
        Color sur[1] = { Color(0, 255, 214, 120) };
        INT nSur = 1;
        pb.SetSurroundColors(sur, &nSur);
        g.FillEllipse(&pb, sx - 55.f, sy - 62.f, 110.f, 110.f);
    }
    // 穹顶
    {
        LinearGradientBrush lb(PointF(sx, sy - 44), PointF(sx, sy - 16),
            Color(255, 248, 214, 120), Color(255, 214, 154, 38));
        g.FillEllipse(&lb, sx - 11.f, sy - 40.f, 22.f, 24.f);
        g.DrawEllipse(&Pen(Color(255, 138, 93, 20), 1.2f), sx - 11.f, sy - 40.f, 22.f, 24.f);
        g.DrawLine(&Pen(Color(255, 138, 93, 20), 1.4f), sx, sy - 40, sx, sy - 48);
        SolidBrush bb(Color(255, 240, 200, 110));
        g.FillEllipse(&bb, sx - 2.5f, sy - 51.f, 5.f, 5.f);
    }
}

static void drawChar(Graphics& g, float cfx, float cfy, float chf) {
    float sx, sy; topCenter(cfx, cfy, chf, sx, sy);
    // 行走时轻微起伏
    float bob = (g_state == ST_WALK) ? -3.f * sinf(g_walkT * PI) : 0.f;
    sy += bob;
    // 影子(不随 bob 移动)
    SolidBrush shb(Color(50, 60, 50, 80));
    g.FillEllipse(&shb, sx - 9.f, sy - bob - 2.f, 18.f, 8.f);
    // 白裙圆锥身(修长, 参考正版 Ida)
    PointF dress[3] = { PointF(sx - 5.5f, sy - 3), PointF(sx + 5.5f, sy - 3), PointF(sx, sy - 26) };
    LinearGradientBrush lb(PointF(sx, sy - 26), PointF(sx, sy - 3),
        Color(255, 255, 255, 255), Color(255, 222, 224, 240));
    g.FillPolygon(&lb, dress, 3);
    g.DrawPolygon(&Pen(Color(170, 150, 155, 175), 1.0f), dress, 3);
    // 头
    SolidBrush hb(Color(255, 255, 255, 255));
    g.FillEllipse(&hb, sx - 3.8f, sy - 33.f, 7.6f, 7.6f);
    g.DrawEllipse(&Pen(Color(170, 150, 155, 175), 1.0f), sx - 3.8f, sy - 33.f, 7.6f, 7.6f);
    // 圆锥帽 (Ida 标志, 更高更挺)
    PointF hat[3] = { PointF(sx - 4.2f, sy - 31.5f), PointF(sx + 4.2f, sy - 31.5f), PointF(sx, sy - 47.f) };
    g.FillPolygon(&hb, hat, 3);
    g.DrawPolygon(&Pen(Color(170, 150, 155, 175), 1.0f), hat, 3);
}

struct TileR { float fx, fy; float h; int kind; };

static void render(HDC dc) {
    g_pal = g_pals[g_cur];   // 每关一套配色
    Graphics gr(dc);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetPixelOffsetMode(PixelOffsetModeHalf);
    gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    // ---- 天空渐变 ----
    {
        LinearGradientBrush lb(PointF(0, 0), PointF(0, (float)WINH), g_pal.skyTop, g_pal.skyBot);
        gr.FillRectangle(&lb, 0, 0, WINW, WINH);
    }
    // ---- 太阳柔光 ----
    {
        GraphicsPath p; p.AddEllipse(WINW - 320, 20, 320, 320);
        PathGradientBrush pb(&p);
        pb.SetCenterPoint(PointF(WINW - 160, 180));
        Color cs[1] = { Color(95, 255, 244, 214) };
        pb.SetCenterColor(cs[0]);
        Color sur[1] = { Color(0, 255, 244, 214) };
        INT nSur = 1;
        pb.SetSurroundColors(sur, &nSur);
        gr.FillEllipse(&pb, WINW - 320, 20, 320, 320);
    }
    // ---- 层叠山峰(远近两层, 每座双色棱面, 参考扁平插画风格) ----
    {
        struct Peak { float cx, py, base, w; };
        auto drawPeaks = [&](const Peak* ps, int n, Color baseC, BYTE alpha) {
            Color lt = Color(alpha,
                (BYTE)min(255.f, baseC.GetR() * 1.18f + 20.f),
                (BYTE)min(255.f, baseC.GetG() * 1.18f + 20.f),
                (BYTE)min(255.f, baseC.GetB() * 1.18f + 20.f));
            Color dkc = Shade(baseC, 0.80f);
            dkc = Color(alpha, dkc.GetR(), dkc.GetG(), dkc.GetB());
            for (int i = 0; i < n; ++i) {
                const Peak& p = ps[i];
                // 暗面: 整座三角形
                PointF tri[3] = { PointF(p.cx, p.py), PointF(p.cx - p.w, p.base), PointF(p.cx + p.w, p.base) };
                SolidBrush db(dkc);
                gr.FillPolygon(&db, tri, 3);
                // 亮面: 左侧棱面(脊线从峰顶落到基线 30% 处)
                PointF face[3] = { PointF(p.cx, p.py), PointF(p.cx - p.w, p.base),
                                   PointF(p.cx + p.w * 0.30f, p.base) };
                SolidBrush lb(lt);
                gr.FillPolygon(&lb, face, 3);
            }
        };
        // 颜色由每关配色派生: 天空底色与描边色调和, 保证与关卡主题和谐
        Color mid = Mix(g_pal.skyBot, g_pal.edge, 0.58f);
        static const Peak farP[] = {   // 远层: 地平线上的雾感远山
            {  60, 272, 330, 118 }, { 205, 300, 330, 100 }, { 345, 286, 330, 108 },
            { 515, 303, 330,  96 }, { 648, 280, 330, 112 }, { 818, 298, 330, 104 },
            { 932, 264, 330, 112 } };
        static const Peak nearP[] = {
            { 140, 296, 330, 128 }, { 420, 306, 330, 118 }, { 700, 293, 330, 122 }, { 888, 302, 330, 112 } };
        drawPeaks(farP, 7, Mix(mid, g_pal.skyBot, 0.45f), 185);
        drawPeaks(nearP, 4, mid, 225);
    }
    // ---- 海面 (参考正版: 青绿海面 + 低多边形三角波纹) ----
    {
        const float HORIZON = 330.f;
        Color seaBase = Mix(Color(255, 72, 196, 178), g_pal.edge, 0.18f);   // 经典青绿海面
        LinearGradientBrush sb2(PointF(0, HORIZON), PointF(0, (float)WINH),
            Mix(seaBase, g_pal.skyBot, 0.35f), Shade(seaBase, 0.88f));
        gr.FillRectangle(&sb2, 0.f, HORIZON, (REAL)WINW, (REAL)WINH - HORIZON);
        // 三角波纹: 固定种子伪随机, 明暗交错
        static std::vector<std::array<float, 4>> waves;   // x, y, size, tone
        static bool waveInit = false;
        if (!waveInit) {
            unsigned s = 7;
            auto rnd = [&]() { s = s * 1103515245u + 12345u; return (float)((s >> 16) & 0xff) / 255.f; };
            for (int i = 0; i < 90; ++i) {
                float wx = rnd() * WINW;
                float wy = HORIZON + 8 + rnd() * (WINH - HORIZON - 12);
                float wsz = 8 + rnd() * 20;
                waves.push_back({ wx, wy, wsz, rnd() > 0.5f ? 1.f : 0.f });
            }
            waveInit = true;
        }
        for (auto& w : waves) {
            float wx = w[0], wy = w[1], wsz = w[2];
            PointF tri[3] = { PointF(wx, wy), PointF(wx + wsz, wy + wsz * 0.45f), PointF(wx + wsz * 1.7f, wy) };
            SolidBrush wb(w[3] > 0.5f ? Color(42, 255, 255, 255) : Color(38, 0, 70, 66));
            gr.FillPolygon(&wb, tri, 3);
        }
        // 地平线浪花
        SolidBrush fb2(Color(85, 255, 255, 255));
        for (float fx = 0; fx < WINW; fx += 90.f)
            gr.FillEllipse(&fb2, fx, HORIZON - 3.f, 46.f, 5.f);
    }
    // ---- 云 ----
    {
        SolidBrush cb(Color(70, 255, 255, 255));
        gr.FillEllipse(&cb, 120, 88, 110, 22);
        gr.FillEllipse(&cb, 150, 78, 70, 18);
        gr.FillEllipse(&cb, 640, 130, 130, 24);
        gr.FillEllipse(&cb, 672, 120, 78, 18);
    }

    // ---- 构建渲染列表(旋转中平台用连续插值坐标) ----
    std::vector<TileR> ts;
    for (auto& s : g_stat) {
        TileR t; t.fx = (float)s[0]; t.fy = (float)s[1]; t.h = (float)s[2];
        t.kind = (s[0] == g_gx && s[1] == g_gy) ? 2 : 0;
        ts.push_back(t);
    }
    for (size_t gi = 0; gi < g_rots.size(); ++gi) {
        const Rot& r = g_rots[gi];
        TileR t; t.h = r.h; t.kind = 1;
        if ((int)gi == g_rotGrp && g_state == ST_ROT) {
            float ang = -ease(g_rotT) * PI / 2.f;
            float cs = cosf(ang), sn = sinf(ang);
            t.fx = (float)r.px; t.fy = (float)r.py; ts.push_back(t);
            for (auto& o : r.offs) {
                t.fx = r.px + (float)o.dx * cs - (float)o.dy * sn;
                t.fy = r.py + (float)o.dx * sn + (float)o.dy * cs;
                ts.push_back(t);
            }
        } else {
            t.fx = (float)r.px; t.fy = (float)r.py; ts.push_back(t);
            for (auto& o : r.offs) {
                Off a = rotN(o, r.k);
                t.fx = (float)(r.px + a.dx); t.fy = (float)(r.py + a.dy); ts.push_back(t);
            }
        }
    }
    // 滑块: 平移动画用连续档位插值
    for (size_t si = 0; si < g_slis.size(); ++si) {
        const Sli& s = g_slis[si];
        float t = (float)g_sliT[si];
        if ((int)si == g_sliGrp && g_state == ST_SLI) t += ease(g_sliAnim);
        TileR tt; tt.kind = 3; tt.h = (float)s.bh + t * s.dh;
        tt.fx = (float)s.bx + t * s.dx; tt.fy = (float)s.by + t * s.dy; ts.push_back(tt);
        for (auto& o : s.offs) {
            tt.fx = (float)s.bx + t * s.dx + o.dx;
            tt.fy = (float)s.by + t * s.dy + o.dy;
            ts.push_back(tt);
        }
    }
    // 画家算法: 按对角线深度排序
    for (size_t i = 0; i < ts.size(); ++i)
        for (size_t j = i + 1; j < ts.size(); ++j)
            if (ts[j].fx + ts[j].fy < ts[i].fx + ts[i].fy) std::swap(ts[i], ts[j]);

    // ---- 悬浮岛阴影 (最底层, 一次画完) ----
    {
        SolidBrush sb(Color(34, 40, 36, 70));
        for (auto& t : ts) {
            float sx, sy; topCenter(t.fx, t.fy, t.h, sx, sy);
            float D = (float)(t.h * HSTEP + SIDE);
            gr.FillEllipse(&sb, sx - CW * 0.92f, sy + D - 2, CW * 1.84f, CHH * 1.1f);
        }
    }

    // ---- 滑块轨道提示线 (虚线, 画在地块之下) ----
    for (auto& s : g_slis) {
        float x0, y0, x1, y1;
        topCenter((float)s.bx, (float)s.by, (float)s.bh, x0, y0);
        topCenter((float)(s.bx + s.steps * s.dx), (float)(s.by + s.steps * s.dy),
                  (float)(s.bh + s.steps * s.dh), x1, y1);
        Pen rail(Color(110, 130, 108, 70), 2.f);
        rail.SetDashStyle(DashStyleDash);
        gr.DrawLine(&rail, x0, y0 - 6, x1, y1 - 6);
    }

    // ---- 小人插值位置(乘坐时跟着平台转/滑块动) ----
    float cfx, cfy, chf;
    if (g_state == ST_WALK) {
        cfx = g_wfx + (g_wtx - g_wfx) * ease(g_walkT);
        cfy = g_wfy + (g_wty - g_wfy) * ease(g_walkT);
        chf = g_wfh + (g_wth - g_wfh) * g_walkT;
    } else if (g_state == ST_ROT && g_ride) {
        float ang = -ease(g_rotT) * PI / 2.f;
        float cs = cosf(ang), sn = sinf(ang);
        float px = (float)g_rots[g_rotGrp].px, py = (float)g_rots[g_rotGrp].py;
        cfx = px + (float)g_rdx * cs - (float)g_rdy * sn;
        cfy = py + (float)g_rdx * sn + (float)g_rdy * cs;
        chf = g_rideH;
    } else if (g_state == ST_SLI && g_sRide) {
        const Sli& s = g_slis[g_sliGrp];
        float t = (float)g_sliT[g_sliGrp] + ease(g_sliAnim);
        cfx = (float)s.bx + g_srox + t * s.dx;
        cfy = (float)s.by + g_sroy + t * s.dy;
        chf = (float)s.bh + g_sroh + t * s.dh;
    } else { cfx = (float)g_ccx; cfy = (float)g_ccy; chf = g_chf; }
    float charKey = cfx + cfy;
    bool charDone = false;

    // ---- 画地块, 小人按深度插入 ----
    for (auto& t : ts) {
        if (!charDone && t.fx + t.fy > charKey) { drawChar(gr, cfx, cfy, chf); charDone = true; }
        drawTile(gr, t.fx, t.fy, t.h, t.kind);
    }
    if (!charDone) drawChar(gr, cfx, cfy, chf);

    // ---- 视错觉可达提示: "画面上正好一步"且非普通相邻的块, 画呼吸光圈 ----
    if (g_state == ST_PLAY || g_state == ST_WALK) {
        int chH = tileH(g_ccx, g_ccy);
        float pulse = 0.5f + 0.3f * sinf(GetTickCount() * 0.005f);
        SolidBrush hb(Color((BYTE)(130 * pulse + 40), 255, 255, 255));
        for (auto& c : g_occ) {
            if (c.x == g_ccx && c.y == g_ccy) continue;
            int ddx = c.x - g_ccx, ddy = c.y - g_ccy, ddh = c.h - chH;
            if (abs(ddx) + abs(ddy) == 1 && abs(ddh) <= 1) continue;   // 普通台阶不提示
            int sdx = (ddx - ddy) * CW, sdy = (ddx + ddy) * CHH - ddh * HSTEP;
            if (abs(sdx) != CW || abs(sdy) != CHH) continue;
            float sx, sy; topCenter((float)c.x, (float)c.y, (float)c.h, sx, sy);
            PointF pts[4] = { PointF(sx, sy - CHH * 0.66f), PointF(sx + CW * 0.66f, sy),
                              PointF(sx, sy + CHH * 0.66f), PointF(sx - CW * 0.66f, sy) };
            gr.FillPolygon(&hb, pts, 4);
        }
    }

    // ---- 终点穹顶 ----
    drawGoal(gr);

    // ---- 旋转支点标记 ----
    for (auto& r : g_rots) {
        float sx, sy; topCenter((float)r.px, (float)r.py, r.h, sx, sy);
        SolidBrush wb(Color(235, 255, 255, 255));
        gr.FillEllipse(&wb, sx - 5.5f, sy - 5.5f, 11.f, 11.f);
        gr.DrawEllipse(&Pen(g_pal.rotEdge, 1.8f), sx - 5.5f, sy - 5.5f, 11.f, 11.f);
        SolidBrush db(Color(255, g_pal.rotEdge.GetR(), g_pal.rotEdge.GetG(), g_pal.rotEdge.GetB()));
        gr.FillEllipse(&db, sx - 1.8f, sy - 1.8f, 3.6f, 3.6f);
    }

    // ---- HUD (半透明圆角面板) ----
    SetBkMode(dc, TRANSPARENT);
    auto panel = [&](float x, float y, float w, float h, BYTE a) {
        GraphicsPath p;
        float d = 10;
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x + w - d, y, d, d, 270, 90);
        p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        p.AddArc(x, y + h - d, d, d, 90, 90);
        p.CloseFigure();
        SolidBrush pb(Color(a, 255, 255, 255));
        gr.FillPath(&pb, &p);
        gr.DrawPath(&Pen(Color(a, 255, 255, 255), 1.5f), &p);
    };
    panel(16, 14, 500, 62, 165);
    panel(16, WINH - 84.f, 500, 70, 150);

    auto T = [&](int x, int y, const wchar_t* s) { TextOutW(dc, x, y, s, (int)wcslen(s)); };
    HFONT of1 = (HFONT)SelectObject(dc, g_fontBig);
    SetTextColor(dc, RGB(52, 50, 62));
    T(30, 22, L"纪念碑小谷 · Monument Valley 风格 Demo");
    SelectObject(dc, g_fontSmall);
    SetTextColor(dc, RGB(105, 100, 120));
    wchar_t buf[128];
    swprintf(buf, 128, L"第 %d / 10 关 · %ls", g_cur + 1, g_name);
    T(30, 50, buf);
    T(30, WINH - 76, L"左键点击：青色平台旋转 90°，金色滑块沿线平移（站上去会被带走）");
    T(30, WINH - 54, L"方向键 / WASD 移动：画面上相邻就能走，哪怕悬空、隔着深渊！");
    T(30, WINH - 32, L"R 重开    N 下一关    1-0 跳关    Esc 退出");

    // ---- 通关卡片 ----
    if (g_state == ST_WIN) {
        float cx = WINW / 2.f, cw = 430, chh2 = 120;
        panel(cx - cw / 2, 70, cw, chh2, 240);
        SelectObject(dc, g_fontTitle);
        SetTextColor(dc, RGB(52, 50, 62));
        const wchar_t* msg = (g_cur == 9) ? L"全部通关！" : L"通关！";
        SIZE sz; GetTextExtentPoint32W(dc, msg, (int)wcslen(msg), &sz);
        TextOutW(dc, (int)(cx - sz.cx / 2), 88, msg, (int)wcslen(msg));
        SelectObject(dc, g_fontSmall);
        SetTextColor(dc, RGB(105, 100, 120));
        const wchar_t* sub = (g_cur == 9) ? L"按 R 从第 1 关再玩一遍（N 也可以）" : L"按 N 进入下一关    按 R 重玩本关";
        GetTextExtentPoint32W(dc, sub, (int)wcslen(sub), &sz);
        TextOutW(dc, (int)(cx - sz.cx / 2), 128, sub, (int)wcslen(sub));
    }
    SelectObject(dc, of1);
}

// ---------------- 窗口过程 ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 16, NULL);
        return 0;
    case WM_TIMER:
        if (g_state == ST_ROT) {
            g_rotT += 16.f / 350.f;
            if (g_rotT >= 1.f) {
                Rot& r = g_rots[g_rotGrp];
                if (g_ride) {
                    Off a = rot1({ g_rdx, g_rdy });
                    g_ccx = r.px + a.dx; g_ccy = r.py + a.dy;
                    g_chf = (float)r.h;
                }
                r.k = (r.k + 1) % 4;
                buildOcc();
                g_rotT = 0.f; g_rotGrp = -1; g_ride = false;
                g_state = ST_PLAY;
            }
        } else if (g_state == ST_SLI) {
            g_sliAnim += 16.f / 300.f;
            if (g_sliAnim >= 1.f) {
                Sli& s = g_slis[g_sliGrp];
                int nt = (g_sliT[g_sliGrp] + 1) % (s.steps + 1);
                if (g_sRide) {   // 乘坐: 小人跟着滑块一起走(到头了会随平台折返)
                    g_ccx = s.bx + g_srox + nt * s.dx;
                    g_ccy = s.by + g_sroy + nt * s.dy;
                    g_chf = (float)(s.bh + g_sroh + nt * s.dh);
                }
                g_sliT[g_sliGrp] = nt;
                buildOcc();
                g_sliAnim = 0.f; g_sliGrp = -1; g_sRide = false;
                g_state = ST_PLAY;
            }
        } else if (g_state == ST_WALK) {
            g_walkT += 16.f / 170.f;
            if (g_walkT >= 1.f) {
                g_ccx = g_wtx; g_ccy = g_wty; g_chf = (float)g_wth;
                g_state = ST_PLAY;
                if (g_ccx == g_gx && g_ccy == g_gy) g_state = ST_WIN;
                else continueHeldKeys();
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, WINW, WINH);
        HBITMAP old = (HBITMAP)SelectObject(mem, bm);
        render(mem);
        BitBlt(dc, 0, 0, WINW, WINH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bm); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (g_state != ST_PLAY) return 0;
        int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        for (size_t gi = 0; gi < g_rots.size(); ++gi) {
            const Rot& r = g_rots[gi];
            bool hit = false;
            auto test = [&](int x, int y) {
                float sx, sy; topCenter((float)x, (float)y, r.h, sx, sy);
                float dx = (float)mx - sx, dy = (float)my - sy;
                if (fabsf(dx) / CW + fabsf(dy) / CHH <= 1.02f) hit = true;
            };
            test(r.px, r.py);
            for (auto& o : r.offs) { Off a = rotN(o, r.k); test(r.px + a.dx, r.py + a.dy); }
            if (hit) {
                g_rotGrp = (int)gi; g_rotT = 0.f; g_state = ST_ROT;
                g_ride = charOnGroup((int)gi);
                if (g_ride) {
                    g_rdx = g_ccx - r.px; g_rdy = g_ccy - r.py;
                    g_rideH = (float)r.h;
                }
                break;
            }
        }
        if (g_state == ST_PLAY) {   // 再试滑块
            for (size_t si = 0; si < g_slis.size(); ++si) {
                const Sli& s = g_slis[si];
                int t = g_sliT[si];
                bool hit = false;
                auto test = [&](int x, int y, int h) {
                    float sx, sy; topCenter((float)x, (float)y, (float)h, sx, sy);
                    float dx = (float)mx - sx, dy = (float)my - sy;
                    if (fabsf(dx) / CW + fabsf(dy) / CHH <= 1.02f) hit = true;
                };
                test(s.bx + t * s.dx, s.by + t * s.dy, s.bh + t * s.dh);
                for (auto& o : s.offs)
                    test(s.bx + t * s.dx + o.dx, s.by + t * s.dy + o.dy, s.bh + t * s.dh);
                if (hit) {
                    g_sliGrp = (int)si; g_sliAnim = 0.f; g_state = ST_SLI;
                    g_sRide = charOnSli((int)si);
                    if (g_sRide) {
                        g_srox = g_ccx - (s.bx + t * s.dx);
                        g_sroy = g_ccy - (s.by + t * s.dy);
                        g_sroh = (int)(g_chf + 0.5f) - (s.bh + t * s.dh);
                    }
                    break;
                }
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp >= '1' && wp <= '9') { loadLevel((int)(wp - '1')); return 0; }
        if (wp == '0') { loadLevel(9); return 0; }
        switch (wp) {
        case VK_UP: case 'W': tryMove(0, -1); break;
        case VK_DOWN: case 'S': tryMove(0, 1); break;
        case VK_LEFT: case 'A': tryMove(-1, 0); break;
        case VK_RIGHT: case 'D': tryMove(1, 0); break;
        case 'R': loadLevel(g_cur); break;
        case 'N':
            if (g_state == ST_WIN) { loadLevel((g_cur + 1) % 10); }
            break;
        case VK_ESCAPE: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    SetProcessDPIAware();   // 避免系统 DPI 缩放把窗口拉伸模糊
    // GDI+ 初始化
    GdiplusStartupInput gdiSI;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiSI, NULL);

    initLevels();
    initPalettes();
    loadLevel(0);

    g_fontTitle = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_fontBig = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"MonumentMiniDemo";
    RegisterClassExW(&wc);

    RECT r = { 0, 0, WINW, WINH };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(0, L"MonumentMiniDemo",
        L"纪念碑小谷 · Monument Valley 风格 Demo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    GdiplusShutdown(gdiToken);
    return (int)m.wParam;
}
