#!/usr/bin/env python3
"""Generate the pad maps used by docs/manual.md.

The layout tables here mirror src/panel.cpp. Keeping the maps generated rather
than hand-drawn is the point: a manual that drifts from the panel is worse than
no manual, and the drift is invisible until someone presses the wrong pad.

    python3 docs/make_maps.py
"""

import os

CELL = 34
GAP = 4
PAD = 26
LABEL_W = 250
BG = "#0b0b0e"
GRID_OFF = "#17171c"
TEXT = "#c9c9d4"
DIM = "#71717f"

V = ["#e03c3c", "#3fd45a", "#4b6bf5", "#e8a72a"]          # voice 1-4
V_DIM = ["#4a1414", "#12401b", "#151d4d", "#4a3508"]
ACTION = "#18a8bf"
DANGER = "#c62b12"
MODIFIER = "#c81ea0"
PLAY = "#1fd455"
STOP = "#d4341a"
CELL_ON = "#b4b4b4"
CELL_OFF = "#1c1c22"

MOD_X, STOP_X, PLAY_X, ROW = 13, 14, 15, 15

PARAM_ROWS = [
    (1, 4, "VOICE", "which voice you are editing"),
    (3, 12, "RATE", "32nd → 8 bars"),
    (5, 11, "RULE", "the 11 selection rules"),
    (7, 4, "ORDER", "fwd / rev / ping / rand"),
    (9, 12, "SYNTH", "which of the 12 presets"),
    (11, 16, "CHAN", "MIDI channel 1-16"),
    (13, 15, "PITCH", "-7 .. +7, centre = 0"),
    (15, 10, "LENGTH", "10% .. 100% of the step"),
]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg_open(w, h, title):
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}" role="img" aria-label="{esc(title)}">',
        f'<rect width="{w}" height="{h}" rx="10" fill="{BG}"/>',
        '<style>'
        'text{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}'
        '.t{font-size:11px;fill:%s}.h{font-size:14px;font-weight:700;fill:%s}'
        '.k{font-size:10px;fill:%s}.p{font-size:9px;font-weight:700}'
        '</style>' % (TEXT, TEXT, DIM),
    ]


def pad(out, x, y, fill, label=None, label_fill="#000", ox=PAD, oy=PAD):
    px = ox + x * (CELL + GAP)
    py = oy + y * (CELL + GAP)
    out.append(f'<rect x="{px}" y="{py}" width="{CELL}" height="{CELL}" rx="7" fill="{fill}"/>')
    if label:
        out.append(
            f'<text class="p" x="{px + CELL/2}" y="{py + CELL/2 + 3.5}" '
            f'text-anchor="middle" fill="{label_fill}">{esc(label)}</text>'
        )


def axes(out, ox=PAD, oy=PAD):
    for i in range(16):
        out.append(
            f'<text class="k" x="{ox + i*(CELL+GAP) + CELL/2}" y="{oy - 8}" '
            f'text-anchor="middle">{i}</text>'
        )
        out.append(
            f'<text class="k" x="{ox - 8}" y="{oy + i*(CELL+GAP) + CELL/2 + 3.5}" '
            f'text-anchor="end">{i}</text>'
        )


def transport(out):
    """Stop and play are DRAWN, not typed: the geometric glyphs fall back to a
    different font and render as mojibake (the square came out as a-grave)."""
    pad(out, MOD_X, ROW, MODIFIER, "\u00d7", "#fff")
    pad(out, STOP_X, ROW, STOP)
    pad(out, PLAY_X, ROW, PLAY)

    sx = PAD + STOP_X * (CELL + GAP)
    sy = PAD + ROW * (CELL + GAP)
    out.append(f'<rect x="{sx + 11}" y="{sy + 11}" width="12" height="12" rx="2" fill="#fff"/>')

    px = PAD + PLAY_X * (CELL + GAP)
    out.append(
        f'<path d="M{px + 12} {sy + 10} L{px + 24} {sy + 17} L{px + 12} {sy + 24} Z" fill="#04240f"/>'
    )


def grid_size(label_w=LABEL_W):
    w = PAD * 2 + 16 * (CELL + GAP) + label_w
    h = PAD * 2 + 16 * (CELL + GAP) + 30
    return w, h


def note(out, x, y, lines):
    for i, (style, s) in enumerate(lines):
        out.append(f'<text class="{style}" x="{x}" y="{y + i*17}">{esc(s)}</text>')


# --------------------------------------------------------------------------- world
def world():
    w, h = grid_size()
    out = svg_open(w, h, "plinky-life world view")
    axes(out)
    live = {(2, 1), (3, 1), (4, 2), (1, 3), (2, 3), (3, 3), (9, 5), (10, 5), (9, 6),
            (10, 6), (6, 9), (7, 9), (8, 9), (12, 11), (13, 11), (12, 12), (5, 13),
            (6, 14), (4, 14), (5, 15), (0, 7), (1, 7), (2, 8)}
    heads = {3: 0, 7: 1, 11: 2, 14: 3}
    for y in range(16):
        for x in range(16):
            if y == ROW and x >= MOD_X:
                continue
            if x in heads:
                v = heads[x]
                fill = V[v] if (x, y) in live else V_DIM[v]
            else:
                fill = CELL_ON if (x, y) in live else CELL_OFF
            pad(out, x, y, fill)
    transport(out)
    lx = PAD + 16 * (CELL + GAP) + 24
    note(out, lx, PAD + 14, [
        ("h", "WORLD"),
        ("t", ""),
        ("t", "Tap any pad to toggle that cell."),
        ("t", ""),
        ("t", "Columns are steps."),
        ("t", "Rows are scale degrees:"),
        ("t", "row 15 is the lowest note,"),
        ("t", "row 0 the highest."),
        ("t", ""),
        ("h", "The four playheads"),
        ("t", "Each voice tints the column"),
        ("t", "it is standing on. A live cell"),
        ("t", "it picks flashes bright."),
        ("t", ""),
        ("t", "An empty column is a rest."),
        ("t", ""),
        ("h", "Row 15, right-hand end"),
        ("t", "(13,15)  actions, hold or tap"),
        ("t", "(14,15)  stop"),
        ("t", "(15,15)  play"),
        ("t", ""),
        ("t", "Transport is the same pad in"),
        ("t", "every mode. Never hidden."),
        ("t", ""),
        ("t", "Those 3 cells still simulate,"),
        ("t", "they just cannot be seen or"),
        ("t", "hand-painted."),
    ])
    out.append("</svg>")
    return "\n".join(out)


# -------------------------------------------------------------------------- action
def action():
    w, h = grid_size()
    out = svg_open(w, h, "plinky-life action layer")
    axes(out)
    for y in range(16):
        for x in range(16):
            if y == ROW and x >= MOD_X:
                continue
            if y == 13 and x < 4:
                pad(out, x, y, V_DIM[x], f"E{x+1}", V[x])
            elif y == 14 and x < 4:
                pad(out, x, y, V[x], f"M{x+1}", "#000")
            elif y == 14 and x < 8:
                pad(out, x, y, V_DIM[x - 4], f"S{x-3}", V[x - 4])
            elif y == 15 and x == 0:
                pad(out, x, y, DANGER, "CLR", "#fff")
            elif y == 15 and x == 1:
                pad(out, x, y, ACTION, "SEE", "#000")
            elif y == 15 and x == 2:
                pad(out, x, y, ACTION, "FRZ", "#000")
            elif y == 15 and x == 3:
                pad(out, x, y, ACTION, "STP", "#000")
            else:
                pad(out, x, y, "#101014")
    transport(out)
    lx = PAD + 16 * (CELL + GAP) + 24
    note(out, lx, PAD + 14, [
        ("h", "ACTIONS - hold or tap (13,15)"),
        ("t", ""),
        ("t", "The world dims underneath so"),
        ("t", "you never lose your place."),
        ("t", ""),
        ("h", "row 13   E1 E2 E3 E4"),
        ("t", "Open that voice's editor."),
        ("t", ""),
        ("h", "row 14   M1..M4   S1..S4"),
        ("t", "M = mute that voice."),
        ("t", "S = solo that voice."),
        ("t", "Lit means you will hear it."),
        ("t", ""),
        ("h", "row 15"),
        ("t", "CLR  wipe the world"),
        ("t", "SEE  sprinkle new cells now"),
        ("t", "FRZ  freeze evolution"),
        ("t", "STP  advance one generation"),
        ("t", ""),
        ("t", "Any action drops you back to"),
        ("t", "the world. E1..E4 open the"),
        ("t", "voice editor instead."),
        ("t", ""),
        ("t", "STP works while stopped, so"),
        ("t", "you can walk the world"),
        ("t", "forward by hand."),
    ])
    out.append("</svg>")
    return "\n".join(out)


# --------------------------------------------------------------------------- voice
def voice():
    w, h = grid_size(300)
    out = svg_open(w, h, "plinky-life voice editor")
    axes(out)
    sel = {1: 1, 3: 4, 5: 7, 7: 0, 9: 1, 11: 1, 13: 4, 15: 5}   # the illustrated voice 2
    rows = {y: (n, name) for y, n, name, _ in PARAM_ROWS}
    for y in range(16):
        for x in range(16):
            if y == ROW and x >= MOD_X:
                continue
            if y not in rows:
                pad(out, x, y, "#0e0e12")
                continue
            n, _ = rows[y]
            if x >= n:
                pad(out, x, y, "#0e0e12")
            elif y == 1:
                pad(out, x, y, V[x] if x == sel[y] else V_DIM[x])
            elif x == sel[y]:
                pad(out, x, y, V[1])
            elif y == 13 and x == 7:
                pad(out, x, y, "#33333c")
            else:
                pad(out, x, y, V_DIM[1])
    transport(out)
    lx = PAD + 16 * (CELL + GAP) + 20
    ly = PAD
    out.append(f'<text class="h" x="{lx}" y="{ly + 14}">VOICE EDITOR</text>')
    out.append(f'<text class="t" x="{lx}" y="{ly + 34}">(13,15) then E1..E4</text>')
    for y, n, name, desc in PARAM_ROWS:
        py = PAD + y * (CELL + GAP) + CELL / 2 + 4
        out.append(f'<text class="h" x="{lx}" y="{py - 2}">{esc(name)}</text>')
        out.append(f'<text class="k" x="{lx}" y="{py + 12}">{esc(desc)}</text>')
    out.append(
        f'<text class="t" x="{PAD}" y="{PAD + 16*(CELL+GAP) + 18}">'
        'Selected pad bright; the rest of the row dim so you can see how far the range goes. '
        'Blank rows keep it readable.</text>'
    )
    out.append("</svg>")
    return "\n".join(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, "img")
    os.makedirs(outdir, exist_ok=True)
    for name, fn in (("world", world), ("action", action), ("voice", voice)):
        path = os.path.join(outdir, name + ".svg")
        with open(path, "w") as f:
            f.write(fn())
        print("wrote", os.path.relpath(path, os.path.dirname(here)))


if __name__ == "__main__":
    main()
