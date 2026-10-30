#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KK_UI 标准 Demo 字模覆盖自检。

缺字在 u8g2 渲染路径下是静默失败：oled_get_glyph_or_space() 会退化成空格度量、
不画任何像素，屏幕上只表现为一处空白，编译和运行都不报错。所以这里对 Demo
实际会用到的每个字符做一次查找。

关键点：本检查不是去字节流里“碰运气搜码点”，而是把 kk_oled_font.c 的
oled_find_glyph() 忠实移植过来，用与固件完全相同的寻址方式查询：
  - 码点 <= 0xFF：从 font+23 起走单字节记录表，cursor += cursor[1]，
                  cursor[1] == 0 表示表尾；start_upper/start_lower 是跳转点。
  - 码点 > 0xFF ：从 font+23 + start_unicode 起走 4 字节跳转表定位区段，
                  再按 [be16 码点][1 字节记录长度] 顺序查找，码点 0 表示区段尾。
因此本脚本同时验证了字模数据在结构上是可遍历的。
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "HARDWARE" / "KK_APP"
FONTS_C = APP / "src" / "kk_ui_fonts.c"
APP_C = APP / "src" / "kk_ui_app.c"
RESOURCES = APP / "font-resources.json"

HEADER_SIZE = 23          # 与 kk_oled_font.c 的 OLED_FONT_HEADER_SIZE 一致
WALK_GUARD = 4096         # 防止畸变字体导致死循环

failures = []
checks = 0


def check(cond, label, detail=""):
    global checks
    checks += 1
    print(f"  [{'ok' if cond else 'FAIL'}]   {label}" + (f"  ({detail})" if detail else ""))
    if not cond:
        failures.append(label)


ESCAPES = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "'": 39}


def decode_c_string(literal):
    """解码 C 字符串字面量内容，支持八进制/十六进制转义。"""
    out = bytearray()
    i = 0
    while i < len(literal):
        ch = literal[i]
        if ch != "\\":
            out.extend(ch.encode("utf-8"))
            i += 1
            continue
        i += 1
        nxt = literal[i]
        if nxt in "01234567":
            j = i
            while j < len(literal) and j < i + 3 and literal[j] in "01234567":
                j += 1
            out.append(int(literal[i:j], 8) & 0xFF)
            i = j
        elif nxt == "x":
            j = i + 1
            while j < len(literal) and literal[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(literal[i + 1:j], 16) & 0xFF)
            i = j
        else:
            out.append(ESCAPES.get(nxt, ord(nxt)))
            i += 1
    return bytes(out)


def parse_font_arrays(path):
    """取出每个 const uint8_t <name>[<n>] 数组的真实字节。

    不能按 ';' 切分整个声明：字模里 0x3B 正是以裸分号写进字符串的，
    必须用同时识别“字符串字面量”和“字面量之外的分号”的词法扫描。
    """
    text = path.read_text(encoding="utf-8-sig")
    fonts = {}
    token = re.compile(r'"((?:[^"\\]|\\.)*)"|;')
    for m in re.finditer(r"const\s+uint8_t\s+(\w+)\s*\[\s*(\d+)\s*\]\s*=", text):
        name, size = m.group(1), int(m.group(2))
        data = bytearray()
        for t in token.finditer(text, m.end()):
            if t.group(0) == ";":
                break
            data.extend(decode_c_string(t.group(1)))
        fonts[name] = {"declared": size, "bytes": bytes(data)}
    return fonts


def be16(blob, i):
    return (blob[i] << 8) | blob[i + 1]


def find_glyph(blob, codepoint):
    """kk_oled_font.c: oled_find_glyph() 的等价实现，返回字形数据起点或 None。"""
    start_upper = be16(blob, 17)
    start_lower = be16(blob, 19)
    start_unicode = be16(blob, 21)
    cursor = HEADER_SIZE

    if codepoint <= 0xFF:
        if codepoint >= ord("a"):
            cursor += start_lower
        elif codepoint >= ord("A"):
            cursor += start_upper
        for _ in range(WALK_GUARD):
            if cursor + 1 >= len(blob):
                return None
            if blob[cursor + 1] == 0:
                return None
            if blob[cursor] == codepoint:
                return cursor + 2
            cursor += blob[cursor + 1]
        return None

    cursor += start_unicode
    lookup = cursor
    for _ in range(WALK_GUARD):
        if lookup + 3 >= len(blob):
            return None
        cursor += be16(blob, lookup)
        indexed = be16(blob, lookup + 2)
        lookup += 4
        if indexed >= codepoint:
            break
    else:
        return None

    for _ in range(WALK_GUARD):
        if cursor + 2 >= len(blob):
            return None
        encoded = be16(blob, cursor)
        if encoded == 0:
            return None
        if encoded == codepoint:
            return cursor + 3
        cursor += blob[cursor + 2]
    return None


def c_literals(path):
    """取出源文件里会被编译的字符串字面量。

    本工程把 KK_UI_REFRESH_MODE 定为 DMA，所以 APP_REFRESH_TEXT 的三分支里
    只有 #else 的 "DMA" 进固件；这里按真实配置只保留该分支，
    避免把未编译的 "Blocking"/"IT" 误报成字模缺失。
    """
    text = path.read_text(encoding="utf-8-sig")
    text = re.sub(
        r"#if\s+KK_UI_REFRESH_MODE\s*==\s*KK_UI_REFRESH_BLOCKING.*?#else(.*?)#endif",
        r"\1", text, flags=re.DOTALL)
    out = []
    for line in text.splitlines():
        if line.lstrip().startswith("#include"):
            continue
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
            out.append(decode_c_string(m.group(1)).decode("utf-8", "replace"))
    return out


# ------------------------------------------------------------------ 载入
fonts = parse_font_arrays(FONTS_C)
res = json.loads(RESOURCES.read_text(encoding="utf-8-sig"))
recorded = {f["array_name"]: {int(c[2:], 16) for c in f["codepoints"]}
            for f in res["fonts"]}
recorded_meta = {f["array_name"]: f for f in res["fonts"]}

print("=" * 72)
print("字模数组")
print("=" * 72)
for name, info in fonts.items():
    print(f"  {name:<18} 声明 {info['declared']}B  字面量 {len(info['bytes'])}B  "
          f"记录字形数 {recorded_meta.get(name, {}).get('glyph_count', '?')}")

print()
print("[0] 数组尺寸与生成记录")
for name, info in fonts.items():
    # C 数组由字符串字面量初始化时，声明长度通常比可见内容多 1 个隐式 NUL。
    sizes = {len(info["bytes"]), len(info["bytes"]) + 1}
    check(info["declared"] in sizes,
          f"{name} 声明长度等于字面量长度或 +1（隐式 NUL）",
          f"{info['declared']} vs {len(info['bytes'])}")
    meta = recorded_meta.get(name)
    if meta:
        check(meta["byte_count"] == info["declared"],
              f"{name} 字节数与 font-resources.json 一致", f"{meta['byte_count']}")

# --------------------------------------- 每个字符串用哪个字模（阅读源码得出）
HOME, TITLE, BODY = "kk_font_ui_home", "kk_font_ui_title", "kk_font_ui_body"

required = {
    HOME: ["设置", "数据监测", "波形", "关于设备"],
    TITLE: ["设置", "组件演示", "数据监测", "关于设备", "波形",
            "FPS:", "0123456789", "-"],
    BODY: [
        "屏幕亮度", "开关", "组件演示", "确认框", "消息框", "Toast",
        "动画间隔", "显示条目", "禁用条目", "操作示例",
        "返回", "取消", "确定", "开", "关", "这是消息框",
        "恢复默认设置？", "包含屏幕亮度", "确定关闭",
        "这是 Toast 提示", "设置已更新", "亮度设置失败", "亮度不支持",
        "操作已触发",
        "运行时间", "开关状态", "亮度控制", "等待", "正常", "不支持", "失败",
        "设备名称", "UI版本", "显示驱动", "屏幕尺寸", "刷新方式", "断电保留",
        "方向键", "确定键", "信息波形", "帧率口径",
        "KK_UI", "0.1.0", "KK_OLED", "320x240", "DMA", "否",
        "选择/调整", "进入/确认", "确定退出", "波形绘制",
        "0123456789", "%", "ms", "s", "自动 ", "手动 ",
    ],
}

print()
print("[1] 用固件同样的查找方式，逐个字符确认字形存在")
for font, strings in required.items():
    blob = fonts[font]["bytes"]
    missing = []
    total = 0
    for s in strings:
        for ch in s:
            total += 1
            if find_glyph(blob, ord(ch)) is None:
                missing.append(f"{ch}(U+{ord(ch):04X})")
    uniq = len({ch for s in strings for ch in s})
    check(not missing, f"{font} 无缺字",
          f"{uniq} 个不同字符 / {total} 次查找" if not missing
          else "缺: " + " ".join(sorted(set(missing))))

print()
print("[2] 与 font-resources.json 的生成记录一致")
for font, strings in required.items():
    have = recorded.get(font, set())
    missing = sorted({f"{ch}(U+{ord(ch):04X})" for s in strings for ch in s
                      if ord(ch) not in have})
    check(not missing, f"{font} 生成记录覆盖所需字符",
          "一致" if not missing else "缺: " + " ".join(missing))

print()
print("[3] 字模里实际存在的字形数与生成记录相符")
for font, meta in recorded_meta.items():
    blob = fonts[font]["bytes"]
    # ASCII 区逐个走记录表，Unicode 区在跳转表命中后用查找函数逐个试
    ascii_count = 0
    c = HEADER_SIZE
    for _ in range(WALK_GUARD):
        if c + 1 >= len(blob) or blob[c + 1] == 0:
            break
        ascii_count += 1
        c += blob[c + 1]
    found = sum(1 for cp in recorded[font] if find_glyph(blob, cp) is not None)
    check(found == meta["glyph_count"],
          f"{font} 记录的全部码点都能在字模中找到",
          f"{found}/{meta['glyph_count']}（ASCII 记录 {ascii_count} 条）")

# ------------------------------- 完整性: 源码里出现的文字是否都在上面的清单内
print()
print("[4] 完整性扫描：kk_ui_app.c 中会编译的字符串字面量")
declared_cp = {ord(c) for strings in required.values() for s in strings for c in s}
declared_cp.update(ord(c) for c in "%sx\nDMA")

extra = {}
for lit in c_literals(APP_C):
    unknown = {c for c in lit if ord(c) not in declared_cp}
    if unknown:
        extra[lit] = unknown
for lit, unk in sorted(extra.items()):
    print(f"  未纳入清单的字面量: {lit!r}  未知字符 {sorted(unk)}")
check(not extra, "源码所有字面量都已被覆盖面清单纳入",
      "无遗漏" if not extra else f"{len(extra)} 处待确认")

# ------------------------------------------------------------------ 结论
print()
print("=" * 72)
if failures:
    print(f"结果: {len(failures)}/{checks} 项不通过")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)
print(f"结果: 全部 {checks} 项字模覆盖自检通过，无缺字")
