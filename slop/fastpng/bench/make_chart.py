# Writes ../speed.svg, the README's chart, from png-bench totals (ms). Update the numbers below
# from a png-bench run and rerun: uv run bench/make_chart.py
import os

# "fastpng, checked" verifies every chunk's CRC and the zlib Adler-32, as wuffs, libpng and lodepng do.
PANELS = [
    ("41 PNGs as their encoders wrote them", [
        ("fastpng", 100.3), ("fastpng, checked", 109.4), ("png crate (Rust)", 181.9), ("libpng", 231.8), ("wuffs", 236.3), ("stb_image", 296.2), ("lodepng", 419.2),
    ]),
    ("The same images, written by fpng (the only files fpng decodes)", [
        ("fastpng", 122.2), ("fastpng, checked", 136.6), ("fpng", 167.6), ("libpng", 243.3), ("wuffs", 320.0), ("png crate (Rust)", 340.5), ("stb_image", 411.7), ("lodepng", 526.1),
    ]),
]

WIDTH = 720
LABEL_W = 132         # decoder names, right-aligned
BAR_X = LABEL_W + 12
BAR_MAX_W = 430       # the longest bar; values go after the bar end
ROW = 28
BAR_H = 18
TITLE_H = 30
PANEL_GAP = 22
TOP = 40

scale_max = max(ms for _, rows in PANELS for _, ms in rows)


def bar_path(x, y, w, h, r=4):
    # Square at the baseline, 4px rounded at the data end.
    r = min(r, w / 2, h / 2)
    return (f"M{x:.1f},{y:.1f} H{x + w - r:.1f} A{r},{r} 0 0 1 {x + w:.1f},{y + r:.1f} "
            f"V{y + h - r:.1f} A{r},{r} 0 0 1 {x + w - r:.1f},{y + h:.1f} H{x:.1f} Z")


parts = []
y = TOP
for title, rows in PANELS:
    rows = sorted(rows, key=lambda row: row[1])
    fastest = dict(rows)["fastpng"]
    parts.append(f'<text class="panel" x="{BAR_X}" y="{y + 16}">{title}</text>')
    y += TITLE_H
    baseline_top = y
    for name, ms in rows:
        w = BAR_MAX_W * ms / scale_max
        cls = "us" if name.startswith("fastpng") else "them"
        cy = y + ROW / 2
        parts.append(f'<text class="name {cls}-text" x="{LABEL_W}" y="{cy + 4.5:.1f}" text-anchor="end">{name}</text>')
        parts.append(f'<path class="{cls}" d="{bar_path(BAR_X, cy - BAR_H / 2, w, BAR_H)}"><title>{name}: {ms:.1f} ms</title></path>')
        ratio = "" if name == "fastpng" else f"  ·  {ms / fastest:.1f}× as long"
        parts.append(f'<text class="value" x="{BAR_X + w + 8:.1f}" y="{cy + 4.5:.1f}">{ms:.1f} ms{ratio}</text>')
        y += ROW
    parts.append(f'<line class="baseline" x1="{BAR_X}" x2="{BAR_X}" y1="{baseline_top}" y2="{y}"/>')
    y += PANEL_GAP

height = y + 4
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" viewBox="0 0 {WIDTH} {height}" font-family="-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif" role="img" aria-labelledby="t">
<title id="t">PNG decode time, total over 41 images and their fpng-written copies (lower is better): fastpng is the fastest of the decoders compared</title>
<style>
  .title {{ font-size: 15px; font-weight: 600; fill: #0b0b0b; }}
  .panel {{ font-size: 12.5px; fill: #52514e; }}
  .name {{ font-size: 13px; fill: #52514e; }}
  .us-text {{ font-weight: 600; fill: #0b0b0b; }}
  .value {{ font-size: 12px; fill: #52514e; font-variant-numeric: tabular-nums; }}
  .surface {{ fill: #fcfcfb; }}
  .us {{ fill: #2a78d6; }}
  .them {{ fill: #8c8b85; }}
  .baseline {{ stroke: #52514e; stroke-width: 1; }}
  @media (prefers-color-scheme: dark) {{
    .title, .us-text {{ fill: #f0efec; }}
    .panel, .name, .value {{ fill: #c3c2b7; }}
    .surface {{ fill: #1a1a19; }}
    .us {{ fill: #3987e5; }}
    .them {{ fill: #6b6a65; }}
    .baseline {{ stroke: #c3c2b7; }}
  }}
</style>
<rect class="surface" width="{WIDTH}" height="{height}" rx="8"/>
<text class="title" x="{BAR_X}" y="22">Total decode time, ms (lower is better) — Apple M4 Max</text>
{chr(10).join(parts)}
</svg>
'''

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "speed.svg")
with open(out, "w") as f:
    f.write(svg)
print(f"wrote {os.path.normpath(out)}")
