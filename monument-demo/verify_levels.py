# -*- coding: utf-8 -*-
# 纪念碑小谷 v3 关卡验证器
# 机制: 旋转平台(可乘坐) + 滑块(沿线平移,可乘坐) + 视错觉连接(画面相邻即可行走)
# 投影: sx=(x-y)*32, sy=(x+y+1)*16 - h*16  (HSTEP=CHH=16)
import collections

CW, CH, HS = 32, 16, 16

def proj(x, y, h):
    return ((x - y) * CW, (x + y + 1) * CH - h * HS)

def rot1(o):
    return (o[1], -o[0])

def rotN(o, k):
    for _ in range(k):
        o = rot1(o)
    return o

# 关卡数据: 与 C++ initLevels() 保持一致
LEVELS = [
# 1 桥
dict(name="桥", w=6, h=5,
     stat=[(0,2,1),(1,2,1),(5,2,2)], s=(0,2), g=(5,2),
     rots=[dict(p=(3,2), k=1, h=1, offs=[(1,0),(-1,0)])],
     slis=[]),
# 2 双桥
dict(name="双桥", w=7, h=6,
     stat=[(0,3,1),(1,3,1),(3,3,2),(5,3,2),(6,3,2)], s=(0,3), g=(6,3),
     rots=[dict(p=(2,2), k=3, h=1, offs=[(0,1)]),
           dict(p=(4,4), k=3, h=2, offs=[(0,-1)])],
     slis=[]),
# 3 折返
dict(name="折返", w=7, h=5,
     stat=[(0,2,1),(1,2,1),(3,2,1),(5,2,2),(5,3,2),(5,4,2),(6,4,2)], s=(0,2), g=(6,4),
     rots=[dict(p=(2,3), k=2, h=1, offs=[(0,-1)]),
           dict(p=(4,1), k=3, h=1, offs=[(0,1)])],
     slis=[]),
# 4 蛇梯
dict(name="蛇梯", w=7, h=7,
     stat=[(3,6,1),(3,5,1),(3,3,1),(3,1,2),(2,1,2),(1,1,2)], s=(3,6), g=(1,1),
     rots=[dict(p=(2,4), k=2, h=1, offs=[(1,0)]),
           dict(p=(4,2), k=3, h=1, offs=[(-1,0)])],
     slis=[]),
# 5 电梯: 垂直滑块 + 视错觉跨空登塔
dict(name="电梯", w=8, h=6,
     stat=[(0,3,1),(1,3,1),(4,4,7),(5,4,7),(6,4,7)], s=(0,3), g=(6,4),
     rots=[],
     slis=[dict(p=(2,3,1), d=(0,0,1), steps=2, offs=[])]),
# 6 滑桥: 水平滑块, 站上去被一起带走
dict(name="滑桥", w=7, h=6,
     stat=[(0,4,1),(1,4,1),(5,4,2),(6,4,2)], s=(0,4), g=(6,4),
     rots=[],
     slis=[dict(p=(2,4,1), d=(1,0,0), steps=2, offs=[(1,0)])]),
# 7 错觉阶: 纯视错觉登山 + 旋转平台收尾
dict(name="错觉阶", w=10, h=7,
     stat=[(0,4,1),(1,4,1),(2,4,3),(4,5,5),(5,5,5),(8,5,5),(9,5,5)], s=(0,4), g=(9,5),
     rots=[dict(p=(7,5), k=0, h=5, offs=[(1,0)])],
     slis=[]),
# 8 双滑: 滑桥接电梯, 顶部错觉登塔
dict(name="双滑", w=9, h=8,
     stat=[(0,6,1),(1,6,1),(7,7,7),(8,7,7)], s=(0,6), g=(8,7),
     rots=[],
     slis=[dict(p=(2,6,1), d=(1,0,0), steps=1, offs=[(1,0)]),
           dict(p=(5,6,1), d=(0,0,1), steps=2, offs=[])]),
# 9 塔梯: 旋转平台 + 升降梯 + 错觉
dict(name="塔梯", w=9, h=5,
     stat=[(0,2,1),(1,2,1),(4,2,2),(7,3,7),(8,3,7)], s=(0,2), g=(8,3),
     rots=[dict(p=(3,2), k=1, h=1, offs=[(1,0)])],
     slis=[dict(p=(5,2,2), d=(0,0,1), steps=3, offs=[])]),
# 10 终章: 旋转 + 滑桥 + 升降梯
dict(name="终章", w=10, h=8,
     stat=[(0,6,1),(1,6,1),(4,6,2),(8,6,6),(9,6,6)], s=(0,6), g=(9,6),
     rots=[dict(p=(2,6), k=3, h=1, offs=[(1,0)])],
     slis=[dict(p=(5,6,2), d=(1,0,0), steps=1, offs=[]),
           dict(p=(7,6,3), d=(0,0,1), steps=2, offs=[])]),
]

STEP_OFFS = {(CW, CH), (-CW, -CH), (CW, -CH), (-CW, CH)}

def sli_cells(L, si, t):
    s = L["slis"][si]
    bx, by, bh = s["p"]
    dx, dy, dh = s["d"]
    cells = [(bx + t*dx, by + t*dy, bh + t*dh)]
    for o in s["offs"]:
        cells.append((bx + t*dx + o[0], by + t*dy + o[1], bh + t*dh))
    return cells

def rot_cells(L, ri, k):
    r = L["rots"][ri]
    cells = [(r["p"][0], r["p"][1], r["h"])]
    for o in r["offs"]:
        a = rotN(o, k)
        cells.append((r["p"][0] + a[0], r["p"][1] + a[1], r["h"]))
    return cells

def occ(L, ks, ts):
    cells = set(L["stat"])
    for ri, k in enumerate(ks):
        cells |= set(rot_cells(L, ri, k))
    for si, t in enumerate(ts):
        cells |= set(sli_cells(L, si, t))
    return cells

def neighbors(cells, cur):
    """可行走目标: 网格相邻且|dh|<=1, 或屏幕投影正好相差一步(视错觉)"""
    x, y, h = cur
    pa = proj(x, y, h)
    out = []
    for c in cells:
        if c == cur:
            continue
        nx, ny, nh = c
        dx, dy, dh = nx - x, ny - y, nh - h
        if abs(dx) + abs(dy) == 1 and abs(dh) <= 1:
            out.append(c)
        elif (proj(nx, ny, nh)[0] - pa[0], proj(nx, ny, nh)[1] - pa[1]) in STEP_OFFS:
            out.append(c)
    return out

def solve(L):
    nR, nS = len(L["rots"]), len(L["slis"])
    h0 = next((sh for (sx, sy, sh) in L["stat"] if (sx, sy) == L["s"]), 1)
    start = (tuple(r["k"] for r in L["rots"]), tuple(0 for _ in L["slis"]),
             (L["s"][0], L["s"][1], h0))
    gx, gy = L["g"]
    dist = {start: 0}
    dq = collections.deque([start])
    while dq:
        st = dq.popleft()
        d = dist[st]
        ks, ts, cur = st
        if cur[0] == gx and cur[1] == gy:
            continue
        cells = occ(L, ks, ts)
        # 行走 (cost 0)
        for nxt in neighbors(cells, cur):
            ns = (ks, ts, nxt)
            if ns not in dist:
                dist[ns] = d
                dq.appendleft(ns)
        # 旋转 (cost 1)
        for ri in range(nR):
            nks = list(ks); nks[ri] = (nks[ri] + 1) % 4; nks = tuple(nks)
            ncur = cur
            if cur in rot_cells(L, ri, ks[ri]):   # 乘坐
                r = L["rots"][ri]
                a = rot1((cur[0] - r["p"][0], cur[1] - r["p"][1]))
                ncur = (r["p"][0] + a[0], r["p"][1] + a[1], r["h"])
            if not (0 <= ncur[0] < L["w"] and 0 <= ncur[1] < L["h"] and ncur[2] >= 0):
                continue
            if ncur not in occ(L, nks, ts):
                continue
            ns = (nks, ts, ncur)
            if ns not in dist:
                dist[ns] = d + 1
                dq.append(ns)
        # 滑动 (cost 1)
        for si in range(nS):
            s = L["slis"][si]
            nts = list(ts); nts[si] = (nts[si] + 1) % (s["steps"] + 1); nts = tuple(nts)
            ncur = cur
            if cur in sli_cells(L, si, ts[si]):   # 乘坐
                base = sli_cells(L, si, ts[si])[0]
                nb = sli_cells(L, si, nts[si])[0]
                ncur = (nb[0] + cur[0] - base[0], nb[1] + cur[1] - base[1], nb[2])
            if not (0 <= ncur[0] < L["w"] and 0 <= ncur[1] < L["h"] and ncur[2] >= 0):
                continue
            if ncur not in occ(L, ks, nts):
                continue
            ns = (ks, nts, ncur)
            if ns not in dist:
                dist[ns] = d + 1
                dq.append(ns)
    best = None
    for st, dd in dist.items():
        if st[2][0] == gx and st[2][1] == gy:
            if best is None or dd < best:
                best = dd
    # 初始(不点击任何机关)仅行走能否到达
    ks0 = tuple(r["k"] for r in L["rots"])
    ts0 = tuple(0 for _ in L["slis"])
    seen = {start[2]}
    dq2 = collections.deque([start[2]])
    zero = False
    cells0 = occ(L, ks0, ts0)
    while dq2:
        cur = dq2.popleft()
        if cur[0] == gx and cur[1] == gy:
            zero = True
            break
        for nxt in neighbors(cells0, cur):
            if nxt not in seen:
                seen.add(nxt)
                dq2.append(nxt)
    return best, zero

print(f"{'#':<3}{'名称':<8}{'最少机关数':<10}{'初始即通?':<10}{'越界检查'}")
ok = True
for i, L in enumerate(LEVELS, 1):
    m, zero = solve(L)
    issues = []
    for ri, r in enumerate(L["rots"]):
        for k in range(4):
            for (x, y, h) in rot_cells(L, ri, k):
                if not (0 <= x < L["w"] and 0 <= y < L["h"]):
                    issues.append(f"rot{ri}臂越界k={k}")
    for si, s in enumerate(L["slis"]):
        for t in range(s["steps"] + 1):
            for (x, y, h) in sli_cells(L, si, t):
                if not (0 <= x < L["w"] and 0 <= y < L["h"]) or h < 0:
                    issues.append(f"sli{si}越界t={t}")
    issues = sorted(set(issues))
    if zero or m is None or issues:
        ok = False
    print(f"{i:<3}{L['name']:<8}{m!s:<10}{'是!!' if zero else '否':<10}{'; '.join(issues) or '无'}")
print("\n全部通过!" if ok else "\n存在问题, 需要修关卡!")
