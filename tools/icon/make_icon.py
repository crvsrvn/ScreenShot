"""生成程序图标：金色渐变圆角底板 + 深棕取景框 + 中心点（与标题栏徽标同一造型）。

同一份几何同时输出 app.svg（矢量源图）和 app.ico（多尺寸），颜色取自 src/app/Theme.cpp：
底板为 accent 渐变 #F6E78C → #E0AD32，描边为 border #D2B340，前景为 onAccent #302500。
32px 及以下的尺寸把线条对齐到整像素、去掉描边，保证任务栏 / 资源管理器小图标清晰。

用法：python tools/icon/make_icon.py [输出目录]（默认 src/app/icons）
"""
import pathlib
import sys

from PIL import Image, ImageDraw

GOLD_TOP = (0xF6, 0xE7, 0x8C)
GOLD_BOTTOM = (0xE0, 0xAD, 0x32)
BORDER = (0xD2, 0xB3, 0x40)
INK = (0x30, 0x25, 0x00)

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SNAP_MAX = 32  # 不大于此尺寸时对齐整像素
SUPERSAMPLE = 8

# 256 设计网格上的几何
TILE_MARGIN, TILE_RADIUS, BORDER_WIDTH = 12, 56, 4
STROKE = 22
FRAME_INSET = 64  # 取景框线条中心离边缘的距离
ARM = 46  # 取景框每条臂从拐角到端点（线条中心）的长度
DOT_RADIUS = 22


def geometry(size):
    """返回目标像素单位下的几何；小尺寸取整到像素格并改用方头线条。"""
    k = size / 256
    snap = size <= SNAP_MAX
    stroke = max(2, round(STROKE * k)) if snap else STROKE * k
    outer = FRAME_INSET * k - STROKE * k / 2  # 线条外沿
    arm = (ARM + STROKE) * k  # 拐角外沿到端点外沿（含两端各半个线宽）
    if snap:
        outer, arm = round(outer), round(arm)
    return {
        "margin": 0 if snap else TILE_MARGIN * k,
        "radius": max(2, round(TILE_RADIUS * k)) if snap else TILE_RADIUS * k,
        "border": 0 if snap else BORDER_WIDTH * k,
        "stroke": stroke,
        "outer": outer,
        "arm": arm,
        "cap": 0 if snap else stroke / 2,
        "dot": max(1.5, DOT_RADIUS * k),
    }


def bracket_bars(size, g):
    """四个 L 形，每个由横竖两段矩形组成：(x0, y0, x1, y1)，目标像素单位。"""
    o, a, w = g["outer"], g["arm"], g["stroke"]
    bars = []
    for left in (True, False):
        for top in (True, False):
            x0 = o if left else size - o - a
            y0 = o if top else size - o - a
            vx = o if left else size - o - w
            hy = o if top else size - o - w
            bars.append((x0, hy, x0 + a, hy + w))  # 横
            bars.append((vx, y0, vx + w, y0 + a))  # 竖
    return bars


def render(size):
    g = geometry(size)
    s = SUPERSAMPLE
    big = size * s

    # 对角渐变：先按目标尺寸逐像素算，再放大到超采样尺寸
    gradient = Image.new("RGBA", (size, size))
    px = gradient.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            px[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(GOLD_TOP, GOLD_BOTTOM)) + (255,)
    gradient = gradient.resize((big, big), Image.BILINEAR)

    m = g["margin"] * s
    tile = (m, m, big - 1 - m, big - 1 - m)
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle(tile, g["radius"] * s, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(gradient, (0, 0), mask)

    d = ImageDraw.Draw(img)
    if g["border"]:
        d.rounded_rectangle(tile, g["radius"] * s, outline=BORDER + (255,), width=round(g["border"] * s))
    for x0, y0, x1, y1 in bracket_bars(size, g):
        d.rounded_rectangle((x0 * s, y0 * s, x1 * s - 1, y1 * s - 1), g["cap"] * s, fill=INK + (255,))
    c, r = big / 2, g["dot"] * s
    d.ellipse((c - r, c - r, c + r, c + r), fill=INK + (255,))
    return img.resize((size, size), Image.LANCZOS)


def hex_color(rgb):
    return "#%02X%02X%02X" % rgb


def svg():
    h = hex_color
    w = 256 - 2 * TILE_MARGIN
    i, e, a = FRAME_INSET, 256 - FRAME_INSET, ARM
    path = (f"M{i} {i + a}V{i}h{a}M{e - a} {i}h{a}v{a}"
            f"M{e} {e - a}v{a}h{-a}M{i + a} {e}H{i}v{-a}")
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{h(GOLD_TOP)}"/>
      <stop offset="1" stop-color="{h(GOLD_BOTTOM)}"/>
    </linearGradient>
  </defs>
  <rect x="{TILE_MARGIN}" y="{TILE_MARGIN}" width="{w}" height="{w}" rx="{TILE_RADIUS}" fill="url(#gold)" stroke="{h(BORDER)}" stroke-width="{BORDER_WIDTH}"/>
  <path d="{path}" fill="none" stroke="{h(INK)}" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="128" cy="128" r="{DOT_RADIUS}" fill="{h(INK)}"/>
</svg>
"""


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "src" / "app" / "icons"
    out.mkdir(parents=True, exist_ok=True)
    (out / "app.svg").write_text(svg(), encoding="utf-8", newline="\n")
    images = [render(s) for s in SIZES]
    images[-1].save(out / "app.ico", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {out / 'app.svg'} and {out / 'app.ico'}")


if __name__ == "__main__":
    main()
