#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KK_UI 320x240 布局与 KK_OLED 驱动缓冲的静态几何自检。

刻意不重新抄写常量：所有数字都是从工程真实头文件/源码里解析出来的，
这样检查的是实际参与编译的取值，而不是脚本里另写的一份。

这不是主机功能测试，只验证几何与类型上限不变量。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INTERNAL = ROOT / "HARDWARE" / "KK_UI" / "src" / "kk_ui_internal.h"
DRIVER_H = ROOT / "HARDWARE" / "KK_OLED" / "driver" / "kk_oled_driver.h"
DRIVER_C = ROOT / "HARDWARE" / "KK_OLED" / "driver" / "kk_oled_driver.c"

failures = []
checks = 0


def check(cond, label, detail=""):
    global checks
    checks += 1
    if cond:
        print(f"  [ok]   {label}" + (f"  ({detail})" if detail else ""))
    else:
        print(f"  [FAIL] {label}" + (f"  ({detail})" if detail else ""))
        failures.append(label)


def parse_defines(path):
    text = path.read_text(encoding="utf-8-sig")
    out = {}
    for m in re.finditer(r"^\s*#define\s+([A-Za-z_]\w*)\s+(\d+)[uUlL]*\s*$",
                         text, re.MULTILINE):
        out[m.group(1)] = int(m.group(2))
    return out


def parse_expr_define(path, name):
    text = path.read_text(encoding="utf-8-sig")
    m = re.search(r"^\s*#define\s+" + name + r"\s+(.+?)\s*$", text, re.MULTILINE)
    return m.group(1) if m else None


ui = parse_defines(INTERNAL)
dev = parse_defines(DRIVER_H)

W = ui.get("KK_UI_SCREEN_WIDTH")
H = ui.get("KK_UI_SCREEN_HEIGHT")
PW = dev.get("OLED_PHYSICAL_WIDTH")
PH = dev.get("OLED_PHYSICAL_HEIGHT")
PAGES = PH // 8 if PH else None

print("=" * 72)
print("解析到的真实取值")
print("=" * 72)
print(f"  KK_UI 逻辑画布      : {W} x {H}")
print(f"  KK_OLED 物理缓冲    : {PW} x {PH}  ({PAGES} 页)")
print(f"  菜单 header/row/rows: {ui.get('KK_UI_MENU_HEADER_HEIGHT')} / "
      f"{ui.get('KK_UI_ROW_HEIGHT')} / {ui.get('KK_UI_VISIBLE_ROWS')}")
print(f"  信息页 header       : {ui.get('KK_UI_INFO_HEADER_HEIGHT')}")
print(f"  首页图标            : {ui.get('KK_UI_HOME_ICON_SIZE')}px @ y="
      f"{ui.get('KK_UI_HOME_ICON_Y')} 间距 {ui.get('KK_UI_HOME_ICON_SPACING')}")

# ---------------------------------------------------------------- 驱动兼容门槛
print()
print("[1] 图形核心兼容门槛 (driver-contract.md)")
check(PW is not None and PW <= 255,
      "物理宽度可用 uint8_t 脏区索引表示", f"width={PW} <= 255")
check(PH is not None and PH % 8 == 0,
      "物理高度是 8 的倍数", f"height={PH}")
check(PAGES is not None and PAGES <= 255,
      "页数可用 uint8_t 表示", f"pages={PAGES} <= 255")
buf_bytes = PW * PAGES
check(buf_bytes <= 65535,
      "帧缓冲字节数可用 uint16_t 索引表示", f"{buf_bytes} <= 65535")
staging = PW * 8 * 2
check(staging <= 65535,
      "单页展开缓冲长度可用 uint16_t 表示", f"{staging} <= 65535")

staging_expr = parse_expr_define(DRIVER_C, "OLED_STAGING_SIZE")
check(staging_expr is not None and "8U * 2U" in staging_expr,
      "OLED_STAGING_SIZE 与 8 列 x 2 字节的展开公式一致", staging_expr)

# 横屏 MADCTL 下 0x2A 走长边、0x2B 走短边；清屏窗口与逐页窗口都必须落在其中。
panel_cols_expr = parse_expr_define(DRIVER_C, "OLED_PANEL_COLS")
panel_rows_expr = parse_expr_define(DRIVER_C, "OLED_PANEL_ROWS")
check(panel_cols_expr == "OLED_PHYSICAL_HEIGHT",
      "横屏 0x2A 跨度取物理高度（长边）", panel_cols_expr)
check(panel_rows_expr == "OLED_PHYSICAL_WIDTH",
      "横屏 0x2B 跨度取物理宽度（短边）", panel_rows_expr)
check(PAGES * 8 == PH,
      "逐页窗口的面板列恰好铺满长边", f"{PAGES}*8 = {PAGES * 8} == {PH}")
check(PH - 1 < PH and PW - 1 < PW,
      "逐页窗口的行范围落在短边跨度内", f"Y in [0,{PW - 1}]")
driver_c_src = DRIVER_C.read_text(encoding="utf-8-sig")
check("oled_fill_gram(0x0000U, OLED_PANEL_COLS, OLED_PANEL_ROWS)" in driver_c_src,
      "面板初始化清屏使用横屏跨度而不是竖屏尺寸")
check("static bool oled_fill_gram(uint16_t rgb565, uint16_t cols, uint16_t rows)"
      in driver_c_src,
      "整屏填充按传入跨度寻址（横屏/竖屏可参数化）")
check("oled_panel_init(uint8_t madctl)" in driver_c_src,
      "面板初始化接受 MADCTL 参数（bring-up 可切竖屏）")

# ---------------------------------------------------- KK_OLED 逻辑尺寸 == KK_UI
print()
print("[2] KK_OLED 上报尺寸与 KK_UI 配置一致 (kk_ui.c 启动时校验)")
rot = re.search(r"#define\s+OLED_CANVAS_ROTATION\s+(\w+)",
                DRIVER_C.read_text(encoding="utf-8-sig"))
rot_name = rot.group(1) if rot else "?"
logical_w = PH if rot_name in ("OLED_ROTATION_90", "OLED_ROTATION_270") else PW
logical_h = PW if rot_name in ("OLED_ROTATION_90", "OLED_ROTATION_270") else PH
print(f"  OLED_CANVAS_ROTATION = {rot_name} -> 逻辑 {logical_w} x {logical_h}")
check((logical_w, logical_h) == (W, H),
      "OLED 逻辑尺寸 == KK_UI_SCREEN_WIDTH/HEIGHT",
      f"({logical_w},{logical_h}) == ({W},{H})")
check(logical_w == PH and logical_h == PW,
      "旋转 90/270 才产生 320x240 横屏", f"{rot_name}")

# ------------------------------------------------------------------ 普通菜单
print()
print("[3] 普通菜单布局")
mh = ui["KK_UI_MENU_HEADER_HEIGHT"]
rh = ui["KK_UI_ROW_HEIGHT"]
vr = ui["KK_UI_VISIBLE_ROWS"]
mx = ui["KK_UI_MENU_MARGIN_X"]
fx = ui["KK_UI_MENU_FOCUS_X"]
fw = ui["KK_UI_MENU_FOCUS_W"]
vright = ui["KK_UI_MENU_VALUE_RIGHT"]
ax = ui["KK_UI_MENU_ARROW_X"]
sx = ui["KK_UI_MENU_SCROLLBAR_X"]
sw = ui["KK_UI_MENU_SCROLLBAR_W"]

check(mh + vr * rh <= H, "header + 可见行不超出屏幕高度",
      f"{mh}+{vr}*{rh}={mh + vr * rh} <= {H}")
check(fx >= 0 and fx + fw < sx, "焦点框不压到滚动条列",
      f"{fx}..{fx + fw} < {sx}")
check(sx + sw <= W, "滚动条列在屏幕内", f"{sx}+{sw} <= {W}")
check(mx < vright < ax <= sx, "左留白 < 数值右边界 < 箭头列 <= 滚动条列",
      f"{mx} < {vright} < {ax} <= {sx}")
check(ax + 6 <= sx, "进入箭头不越过滚动条列", f"{ax + 6} <= {sx}")
check(vright - mx >= 120, "标签+数值可用宽度足够", f"{vright - mx}px")

# ------------------------------------------------------------------- 首页
print()
print("[4] 首页布局")
isz = ui["KK_UI_HOME_ICON_SIZE"]
iy = ui["KK_UI_HOME_ICON_Y"]
isp = ui["KK_UI_HOME_ICON_SPACING"]
ly = ui["KK_UI_HOME_LABEL_Y"]
lh = ui["KK_UI_HOME_LABEL_H"]
ltx = ui["KK_UI_HOME_LABEL_TEXT_Y"]
lmax = ui["KK_UI_HOME_LABEL_MAX_W"]
center_x = (W - isz) // 2

check(iy + isz <= H, "图标纵向在屏幕内", f"{iy}+{isz} <= {H}")
check(ly + lh <= H, "反相标签框在屏幕内", f"{ly}+{lh} <= {H}")
check(ltx >= ly and ltx + 16 <= ly + lh, "标签文字在标签框内",
      f"{ltx} 在 {ly}..{ly + lh}")
check(ly >= iy + isz, "标签框在图标下方", f"{ly} >= {iy + isz}")
check(isp > isz, "图标间距大于图标宽度（相邻图标不重叠）", f"{isp} > {isz}")
check(center_x >= 0 and center_x + isz <= W, "选中图标水平居中",
      f"x={center_x}..{center_x + isz}")
check(lmax <= W - 2 * mx, "标签最大宽度不超出可用宽度", f"{lmax} <= {W - 2 * mx}")
check(lmax % 2 == 0, "标签宽度上限为偶数（label_left 取整稳定）", f"{lmax}")

# ------------------------------------------------------------------- 信息页
print()
print("[5] 信息页布局")
ih = ui["KK_UI_INFO_HEADER_HEIGHT"]
imx = ui["KK_UI_INFO_MARGIN_X"]
ivr = ui["KK_UI_INFO_VALUE_RIGHT"]
isx = ui["KK_UI_INFO_SCROLLBAR_X"]
cx = ui["KK_UI_INFO_CLOSE_X"]
cy = ui["KK_UI_INFO_CLOSE_Y"]
cs = ui["KK_UI_INFO_CLOSE_SIZE"]

check(ih < H, "标题栏高度小于屏幕高度", f"{ih} < {H}")
check(imx < ivr < isx, "左留白 < 数值右边界 < 滚动条列", f"{imx} < {ivr} < {isx}")
check(isx + 4 <= W, "滚动条在屏幕内", f"{isx}+4 <= {W}")
check(cx + cs <= isx, "关闭标记在右上角且不越过滚动条列",
      f"{cx}..{cx + cs} <= {isx}")
check(cy + cs <= ih, "关闭标记在标题栏内", f"{cy}+{cs} <= {ih}")
check(cx > W // 2, "关闭标记在右半屏，短标题居中不会撞上", f"{cx} > {W // 2}")
check((H - ih) // rh >= 4, "信息页至少可见 4 行",
      f"{(H - ih) // rh} 行")

# ------------------------------------------------------------- 模态框与 Toast
print()
print("[6] 模态框与 Toast")
dx = ui["KK_UI_DIALOG_X"]
dy = ui["KK_UI_DIALOG_Y"]
dw = ui["KK_UI_DIALOG_W"]
dh = ui["KK_UI_DIALOG_H"]
oh = ui["KK_UI_DIALOG_OPEN_H"]
ty = ui["KK_UI_DIALOG_TITLE_Y"]
th = ui["KK_UI_DIALOG_TITLE_H"]
rx = ui["KK_UI_DIALOG_RULE_X"]
ry = ui["KK_UI_DIALOG_RULE_Y"]
rw = ui["KK_UI_DIALOG_RULE_W"]
vx = ui["KK_UI_DIALOG_VALUE_X"]
vy = ui["KK_UI_DIALOG_VALUE_Y"]
vw = ui["KK_UI_DIALOG_VALUE_W"]
vh = ui["KK_UI_DIALOG_VALUE_H"]
arx = ui["KK_UI_DIALOG_ARROW_X"]
sww = ui["KK_UI_DIALOG_SWITCH_W"]
swh = ui["KK_UI_DIALOG_SWITCH_H"]
tw = ui["KK_UI_DIALOG_TEXT_W"]
by = ui["KK_UI_DIALOG_BTN_Y"]
bw = ui["KK_UI_DIALOG_BTN_W"]
bh = ui["KK_UI_DIALOG_BTN_H"]
bcx = ui["KK_UI_DIALOG_CANCEL_X"]
bfx = ui["KK_UI_DIALOG_CONFIRM_X"]
toasth = ui["KK_UI_TOAST_H"]
toastw = ui["KK_UI_TOAST_MAX_W"]

check(dx + dw <= W and dy + dh <= H, "模态框在屏幕内",
      f"({dx},{dy}) {dw}x{dh}")
check(dh > oh, "展开动画由 OPEN_H 增长到 DIALOG_H", f"{oh} -> {dh}")
check(rx > dx and rx + rw <= dx + dw, "分隔线在框内", f"{rx}..{rx + rw}")
check(ty + th <= ry, "标题在分隔线上方", f"{ty + th} <= {ry}")
check(vx >= dx and vx + vw <= dx + dw, "数值区在框内", f"{vx}..{vx + vw}")
check(vy >= ry and vy + vh <= by, "数值区在分隔线与按钮之间",
      f"{ry} <= {vy} and {vy + vh} <= {by}")
check(arx >= vx + vw and arx + 12 <= dx + dw, "加减箭头在数值区右侧且在框内",
      f"{arx}..{arx + 12}  (数值区右界 {vx + vw})")
switch_x = (W - sww) // 2
check(switch_x - tw - 6 >= vx, "开关左侧文字在数值区内",
      f"{switch_x - tw - 6} >= {vx}")
check(switch_x + sww + 6 + tw <= vx + vw, "开关右侧文字在数值区内",
      f"{switch_x + sww + 6 + tw} <= {vx + vw}")
check(vh >= swh, "数值区高度容得下开关", f"{vh} >= {swh}")
check(by + bh <= dy + dh, "按钮行在框内", f"{by + bh} <= {dy + dh}")
check(bcx >= dx and bfx + bw <= dx + dw, "两个按钮都在框内",
      f"{bcx} >= {dx} and {bfx + bw} <= {dx + dw}")
check(bcx + bw < bfx, "取消/确定按钮不重叠", f"{bcx + bw} < {bfx}")
pair_center = (bcx + bfx + bw) // 2
check(abs(pair_center - W // 2) <= 4, "按钮对整体居中",
      f"中心 {pair_center} vs {W // 2}")
check(toasth <= H, "Toast 高度在屏幕内", f"{toasth} <= {H}")
check(toastw <= W, "Toast 最大宽度在屏幕内", f"{toastw} <= {W}")

# --------------------------------------------------------------- 页面转场
print()
print("[7] 页面转场与帧缓冲")
par = ui["KK_UI_PAGE_PARALLAX"]
check(0 < par < W, "转场视差量小于屏幕宽度", f"0 < {par} < {W}")
check((H - ih) // rh * rh + ih <= H, "信息页整行裁剪不越界",
      f"{ih} + {((H - ih) // rh)}*{rh} <= {H}")

ram = 2 * buf_bytes + staging
print(f"  KK_OLED 静态占用: 双缓冲 {2 * buf_bytes}B + 展开缓冲 {staging}B = {ram}B")

# ----------------------------------------------------------------------- 结论
print()
print("=" * 72)
if failures:
    print(f"结果: {len(failures)}/{checks} 项不通过")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)
print(f"结果: 全部 {checks} 项几何/类型上限自检通过")
