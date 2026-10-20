"""Generate the SoulSilver Online UI sprite set in a Let's Go-inspired flat style.

Every sprite is written as an SVG master plus PNG at 1x (sized for a 1920x1080
screen) and 2x. All artwork is original: simple flat shapes, no game assets.
"""
import json, math, os, sys
from playwright.sync_api import sync_playwright

OUT = sys.argv[1]
FONT = "DejaVu Sans, Arial, sans-serif"

# ---- palette -------------------------------------------------------------
INK = "#3B3F4A"; MUTED = "#8A93A3"; WHITE = "#FFFFFF"; LINE = "#DCE3EC"
LAV = "#B9A6F2"; LAV_L = "#F1ECFF"; YEL = "#FFE04A"; YEL_D = "#E8B400"
TEAL = "#2EC4C6"; TEAL_D = "#1C9CA3"; GREEN = "#5BD15B"; GREEN_D = "#2F9E3F"
RED = "#F0505A"; ORANGE = "#F79A2B"; BLUE = "#3E8EF0"; PURPLE = "#8E6BE8"
PINK = "#F25C8A"; SLATE = "#5B6270"; NAVY = "#1C2A44"
PLAYER = {"blue": "#3D6FD1", "green": "#4CAF50", "orange": "#F2A33A", "red": "#E05252"}

items = []  # dicts: path, svg, w, h, slice, desc


def shadow_filter(fid="sh", dy=4, blur=5, op=0.28, color="#283C5A"):
    return (f'<filter id="{fid}" x="-30%" y="-30%" width="160%" height="160%">'
            f'<feDropShadow dx="0" dy="{dy}" stdDeviation="{blur}" flood-color="{color}" flood-opacity="{op}"/></filter>')


def svg(w, h, body, defs=""):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<defs>{defs}</defs>{body}</svg>')


def add(path, w, h, body, defs="", slice_=None, desc=""):
    items.append({"path": path, "svg": svg(w, h, body, defs), "w": w, "h": h, "slice": slice_, "desc": desc})


# ---- icons (48x48 grid, colour C) ---------------------------------------
def poly(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def star_pts(cx, cy, r1, r2, n, rot=-90):
    pts = []
    for i in range(n * 2):
        r = r1 if i % 2 == 0 else r2
        a = math.radians(rot + i * 180 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def gear_path(cx, cy, r_out, r_in, teeth):
    pts = []
    for i in range(teeth * 4):
        a = math.radians(i * 360 / (teeth * 4) - 90)
        r = r_out if (i % 4) in (1, 2) else r_in
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly(pts)


S = 'stroke-width="4" stroke-linecap="round" stroke-linejoin="round"'

ICONS = {
    "book": lambda C: f'<rect x="11" y="7" width="27" height="34" rx="4" fill="none" stroke="{C}" {S}/><path d="M18 7v34M24 16h8M24 23h8" fill="none" stroke="{C}" {S}/>',
    "bag": lambda C: f'<rect x="8" y="17" width="32" height="24" rx="7" fill="none" stroke="{C}" {S}/><path d="M17 17v-3a7 7 0 0 1 14 0v3M8 27h32" fill="none" stroke="{C}" {S}/><rect x="20" y="24" width="8" height="7" rx="2" fill="{C}"/>',
    "party": lambda C: f'<circle cx="17" cy="17" r="6" fill="none" stroke="{C}" {S}/><circle cx="32" cy="19" r="5" fill="none" stroke="{C}" {S}/><path d="M6 39c1-8 6-12 11-12s10 4 11 12M27 39c1-6 3-10 7-10s7 4 8 10" fill="none" stroke="{C}" {S}/>',
    "chat": lambda C: f'<path d="M7 10h22a4 4 0 0 1 4 4v10a4 4 0 0 1-4 4H16l-7 6v-6H7a4 4 0 0 1-4-4V14a4 4 0 0 1 4-4z" fill="none" stroke="{C}" {S}/><path d="M37 18h4a4 4 0 0 1 4 4v9a4 4 0 0 1-4 4h-1v5l-6-5h-8" fill="none" stroke="{C}" {S}/>',
    "save": lambda C: f'<path d="M9 7h24l7 7v27H9z" fill="none" stroke="{C}" {S}/><path d="M15 7v10h14V7M15 41V29h18v12" fill="none" stroke="{C}" {S}/>',
    "cog": lambda C: f'<polygon points="{gear_path(24,24,19,14,8)}" fill="none" stroke="{C}" {S}/><circle cx="24" cy="24" r="6" fill="none" stroke="{C}" {S}/>',
    "map": lambda C: f'<path d="M6 11l11-4 14 5 11-4v29l-11 4-14-5-11 4z" fill="none" stroke="{C}" {S}/><path d="M17 7v29M31 12v29" fill="none" stroke="{C}" {S}/>',
    "wifi": lambda C: f'<path d="M5 18a27 27 0 0 1 38 0M11 25a18 18 0 0 1 26 0M17 32a9 9 0 0 1 14 0" fill="none" stroke="{C}" {S}/><circle cx="24" cy="39" r="3.5" fill="{C}"/>',
    "card": lambda C: f'<rect x="5" y="10" width="38" height="28" rx="5" fill="none" stroke="{C}" {S}/><circle cx="16" cy="22" r="4.5" fill="none" stroke="{C}" {S}/><path d="M10 32c2-3 4-4 6-4s4 1 6 4M27 19h10M27 26h10" fill="none" stroke="{C}" {S}/>',
    "gamepad": lambda C: f'<path d="M14 14h20c6 0 9 6 10 14s-2 11-6 10-6-6-9-6h-10c-3 0-5 5-9 6s-7-2-6-10 4-14 10-14z" fill="none" stroke="{C}" {S}/><path d="M15 21v8M11 25h8" fill="none" stroke="{C}" {S}/><circle cx="32" cy="22" r="2.5" fill="{C}"/><circle cx="36" cy="27" r="2.5" fill="{C}"/>',
    "play": lambda C: f'<path d="M15 9l24 15-24 15z" fill="none" stroke="{C}" {S}/>',
    "layers": lambda C: f'<path d="M24 6l18 9-18 9-18-9z" fill="none" stroke="{C}" {S}/><path d="M6 24l18 9 18-9M6 33l18 9 18-9" fill="none" stroke="{C}" {S}/>',
    "fight": lambda C: f'<polygon points="{poly(star_pts(24,24,21,9,8,-22.5))}" fill="none" stroke="{C}" {S}/>',
    "run": lambda C: f'<path d="M20 6H9v36h11" fill="none" stroke="{C}" {S}/><path d="M18 24h24M33 15l9 9-9 9" fill="none" stroke="{C}" {S}/>',
    "check": lambda C: f'<path d="M9 25l10 10 20-21" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
    "cross": lambda C: f'<path d="M12 12l24 24M36 12L12 36" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round"/>',
    "question": lambda C: f'<path d="M16 17a8 8 0 1 1 11 7c-2 1-3 3-3 6" fill="none" stroke="{C}" stroke-width="5" stroke-linecap="round"/><circle cx="24" cy="39" r="3.5" fill="{C}"/>',
    "info": lambda C: f'<circle cx="24" cy="24" r="19" fill="none" stroke="{C}" {S}/><path d="M24 22v12" fill="none" stroke="{C}" stroke-width="5" stroke-linecap="round"/><circle cx="24" cy="14.5" r="3" fill="{C}"/>',
    "search": lambda C: f'<circle cx="21" cy="21" r="12" fill="none" stroke="{C}" {S}/><path d="M30 30l11 11" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round"/>',
    "eye": lambda C: f'<path d="M4 24c5-9 12-14 20-14s15 5 20 14c-5 9-12 14-20 14S9 33 4 24z" fill="none" stroke="{C}" {S}/><circle cx="24" cy="24" r="6" fill="{C}"/>',
    "lock": lambda C: f'<rect x="10" y="21" width="28" height="21" rx="5" fill="none" stroke="{C}" {S}/><path d="M16 21v-6a8 8 0 0 1 16 0v6" fill="none" stroke="{C}" {S}/><circle cx="24" cy="31" r="3" fill="{C}"/>',
    "star": lambda C: f'<polygon points="{poly(star_pts(24,25,20,8.5,5))}" fill="none" stroke="{C}" {S}/>',
    "refresh": lambda C: f'<path d="M39 21a15 15 0 1 0-1 10" fill="none" stroke="{C}" {S}/><path d="M40 9v12H28" fill="none" stroke="{C}" {S}/>',
    "arrow_left": lambda C: f'<path d="M30 9L15 24l15 15" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
    "arrow_right": lambda C: f'<path d="M18 9l15 15-15 15" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
    "arrow_up": lambda C: f'<path d="M9 30l15-15 15 15" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
    "arrow_down": lambda C: f'<path d="M9 18l15 15 15-15" fill="none" stroke="{C}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
    "phone": lambda C: f'<rect x="13" y="5" width="22" height="38" rx="5" fill="none" stroke="{C}" {S}/><path d="M21 36h6" fill="none" stroke="{C}" {S}/>',
    "radio": lambda C: f'<rect x="6" y="16" width="36" height="24" rx="5" fill="none" stroke="{C}" {S}/><path d="M14 16l18-9" fill="none" stroke="{C}" {S}/><circle cx="31" cy="28" r="5" fill="none" stroke="{C}" {S}/><path d="M12 24h7M12 31h7" fill="none" stroke="{C}" {S}/>',
    "box": lambda C: f'<path d="M24 5l18 9v20l-18 9-18-9V14z" fill="none" stroke="{C}" {S}/><path d="M6 14l18 9 18-9M24 23v20" fill="none" stroke="{C}" {S}/>',
    "trade": lambda C: f'<path d="M8 17h30M30 9l8 8-8 8M40 31H10M18 23l-8 8 8 8" fill="none" stroke="{C}" {S}/>',
    "footprint": lambda C: f'<ellipse cx="18" cy="27" rx="7" ry="11" fill="{C}"/><ellipse cx="31" cy="20" rx="6" ry="9" fill="{C}"/><circle cx="12" cy="11" r="3" fill="{C}"/><circle cx="20" cy="9" r="3" fill="{C}"/><circle cx="37" cy="6" r="2.5" fill="{C}"/>',
    "bell": lambda C: f'<path d="M12 34V22a12 12 0 0 1 24 0v12l4 4H8z" fill="none" stroke="{C}" {S}/><path d="M20 42h8" fill="none" stroke="{C}" {S}/>',
    "user_plus": lambda C: f'<circle cx="19" cy="16" r="8" fill="none" stroke="{C}" {S}/><path d="M5 41c1-9 7-14 14-14s13 5 14 14" fill="none" stroke="{C}" {S}/><path d="M38 14v12M32 20h12" fill="none" stroke="{C}" {S}/>',
    "exit": lambda C: f'<path d="M22 6H9v36h13" fill="none" stroke="{C}" {S}/><path d="M42 24H20M34 16l8 8-8 8" fill="none" stroke="{C}" {S}/>',
    "clock": lambda C: f'<circle cx="24" cy="24" r="18" fill="none" stroke="{C}" {S}/><path d="M24 13v12l8 5" fill="none" stroke="{C}" {S}/>',
    "dex": lambda C: f'<rect x="8" y="5" width="32" height="38" rx="6" fill="none" stroke="{C}" {S}/><circle cx="24" cy="18" r="7" fill="none" stroke="{C}" {S}/><path d="M15 32h18M15 37h11" fill="none" stroke="{C}" {S}/>',
    "flag": lambda C: f'<path d="M11 43V6M11 8h24l-5 8 5 8H11" fill="none" stroke="{C}" {S}/>',
}

for name, f in ICONS.items():
    add(f"icons/icon_{name}.svg", 48, 48, f(INK), desc=f"{name} icon, ink colour (recolour freely)")
    add(f"icons/white/icon_{name}.svg", 48, 48, f(WHITE), desc=f"{name} icon, white")

# ---- panels (9-slice) ----------------------------------------------------
P = 12  # shadow padding
D = shadow_filter()


def card(w, h, r, fill, stroke=None, sw=0, extra="", flt=True):
    st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
    fl = ' filter="url(#sh)"' if flt else ""
    return f'<rect x="{P}" y="{P-2}" width="{w-2*P}" height="{h-2*P}" rx="{r}" fill="{fill}"{st}{fl}/>{extra}'


add("panels/card.svg", 240, 160, card(240, 160, 24, WHITE, "#E3E9F1", 3), D, [36, 36, 36, 36], "Main white card. 9-slice.")
add("panels/card_glass.svg", 240, 160, card(240, 160, 24, "rgba(255,255,255,0.86)", "rgba(255,255,255,0.95)", 3), D, [36, 36, 36, 36], "Translucent card over the world. 9-slice.")
add("panels/card_header_teal.svg", 240, 180,
    f'<g filter="url(#sh)"><rect x="{P}" y="{P-2}" width="216" height="156" rx="22" fill="{WHITE}" stroke="#E3E9F1" stroke-width="3"/>'
    f'<path d="M{P+1.5} {P+50} V{P+20} a20 20 0 0 1 20 -20.5 H{240-P-21.5} a20 20 0 0 1 20 20.5 V{P+50} z" fill="{TEAL}"/></g>',
    D, [66, 36, 36, 36], "Card with a teal title band (stat boxes, Pokédex entry). 9-slice; title band is the top 66 px.")
add("panels/card_header_yellow.svg", 240, 180,
    f'<g filter="url(#sh)"><rect x="{P}" y="{P-2}" width="216" height="156" rx="22" fill="{WHITE}" stroke="#E3E9F1" stroke-width="3"/>'
    f'<path d="M{P+1.5} {P+50} V{P+20} a20 20 0 0 1 20 -20.5 H{240-P-21.5} a20 20 0 0 1 20 20.5 V{P+50} z" fill="{YEL}"/></g>',
    D, [66, 36, 36, 36], "Card with a yellow title band. 9-slice.")
add("panels/dialogue_box.svg", 480, 160,
    f'<defs><linearGradient id="dg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="{LAV_L}"/></linearGradient></defs>'
    + card(480, 160, 24, "url(#dg)", LAV, 5), D, [40, 40, 40, 40], "Dialogue / message box with the lavender rim. 9-slice.")
add("panels/frame_yellow.svg", 480, 320,
    f'<g filter="url(#sh)"><rect x="{P}" y="{P-2}" width="456" height="296" rx="40" fill="#FFCF3A"/>'
    f'<rect x="{P+22}" y="{P+20}" width="412" height="252" rx="26" fill="#FFF9E8"/></g>',
    D, [60, 60, 60, 60], "Thick yellow frame with a cream field (bag, menus). 9-slice.")
add("panels/frame_aqua.svg", 480, 320,
    f'<g filter="url(#sh)"><rect x="{P}" y="{P-2}" width="456" height="296" rx="40" fill="#7FDCEB"/>'
    f'<rect x="{P+22}" y="{P+20}" width="412" height="252" rx="26" fill="#E9FBFF"/></g>',
    D, [60, 60, 60, 60], "Thick aqua frame (Pokédex, PC box). 9-slice.")

ROW = 56


def row(name, fill, stroke, sw, desc, grad=None):
    defs = D + (grad or "")
    add(f"panels/{name}.svg", 360, ROW + 2 * P - 4,
        f'<rect x="{P}" y="{P-2}" width="{360-2*P}" height="{ROW}" rx="16" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" filter="url(#sh)"/>',
        defs, [28, 30, 28, 30], desc)


row("row", WHITE, LINE, 2, "List row. 9-slice.")
row("row_selected", "url(#yg)", YEL_D, 3, "Selected list row (yellow). 9-slice.",
    f'<linearGradient id="yg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF07A"/><stop offset="1" stop-color="{YEL}"/></linearGradient>')
row("row_disabled", "#EEF1F5", LINE, 2, "Disabled / empty row. 9-slice.")
row("row_teal", "#DDF7F7", TEAL, 3, "Highlighted row, teal (you / your entry). 9-slice.")
row("row_gold", "#FFF4CC", "#F2C14E", 3, "Host row, gold. 9-slice.")
row("row_red", "url(#rg)", "#C8323C", 3, "Selected row, red (shops, destructive). 9-slice.",
    f'<linearGradient id="rg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FF7A80"/><stop offset="1" stop-color="{RED}"/></linearGradient>')

# buttons and pills
add("panels/button_yellow.svg", 320, 72,
    f'<defs><linearGradient id="bg1" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFF07A"/><stop offset=".55" stop-color="{YEL}"/><stop offset="1" stop-color="#FFC800"/></linearGradient></defs>'
    f'<rect x="{P}" y="{P-2}" width="296" height="48" rx="24" fill="url(#bg1)" stroke="{YEL_D}" stroke-width="3" filter="url(#sh)"/>',
    D, [30, 36, 30, 36], "Primary action button (yellow pill). 9-slice.")
add("panels/button_white.svg", 320, 72,
    f'<rect x="{P}" y="{P-2}" width="296" height="48" rx="24" fill="{WHITE}" stroke="{LINE}" stroke-width="3" filter="url(#sh)"/>',
    D, [30, 36, 30, 36], "Secondary action button (white pill). 9-slice.")
add("panels/button_lavender.svg", 320, 72,
    f'<rect x="{P}" y="{P-2}" width="296" height="48" rx="24" fill="{WHITE}" stroke="{LAV}" stroke-width="4" filter="url(#sh)"/>',
    D, [30, 36, 30, 36], "Focused secondary button (lavender rim). 9-slice.")
add("panels/toast.svg", 400, 80,
    f'<rect x="{P}" y="{P-2}" width="376" height="56" rx="28" fill="{WHITE}" stroke="#E3E9F1" stroke-width="3" filter="url(#sh)"/>',
    D, [32, 40, 32, 40], "Notification toast pill. 9-slice.")
add("panels/tab.svg", 180, 72,
    f'<rect x="{P}" y="{P-2}" width="156" height="48" rx="24" fill="{WHITE}" stroke="{LINE}" stroke-width="3" filter="url(#sh)"/>', D, [30, 30, 30, 30], "Tab, idle. 9-slice.")
add("panels/tab_selected.svg", 180, 72,
    f'<rect x="{P}" y="{P-2}" width="156" height="48" rx="24" fill="{TEAL}" stroke="{TEAL_D}" stroke-width="3" filter="url(#sh)"/>', D, [30, 30, 30, 30], "Tab, selected (teal). 9-slice.")
add("panels/hint_bar.svg", 64, 52,
    f'<rect x="0" y="0" width="64" height="52" fill="{WHITE}"/><rect x="0" y="0" width="64" height="3" fill="#E3E9F1"/>',
    "", [4, 0, 0, 0], "Bottom button-hint strip. Stretch horizontally.")
add("panels/code_cell.svg", 80, 96,
    f'<rect x="{P}" y="{P-2}" width="56" height="72" rx="12" fill="{WHITE}" stroke="{LINE}" stroke-width="3" filter="url(#sh)"/>', D, None, "One character cell for code entry.")
add("panels/code_cell_selected.svg", 80, 96,
    f'<rect x="{P}" y="{P-2}" width="56" height="72" rx="12" fill="#FFF7C2" stroke="{YEL_D}" stroke-width="4" filter="url(#sh)"/>', D, None, "Code cell under the cursor.")
add("panels/session_badge.svg", 240, 72,
    f'<rect x="2" y="2" width="236" height="62" rx="10" fill="{NAVY}" stroke="#E8E8E8" stroke-width="4"/><rect x="2" y="64" width="236" height="6" rx="3" fill="#0B1222"/>',
    "", [18, 18, 18, 18], "Session badge background, kept as before (navy). 9-slice.")
add("panels/slot_grid.svg", 96, 96,
    f'<rect x="{P}" y="{P-2}" width="72" height="72" rx="16" fill="{WHITE}" stroke="{LINE}" stroke-width="3" filter="url(#sh)"/>', D, [24, 24, 24, 24], "Grid slot (party, box, bag pockets).")
add("panels/slot_grid_selected.svg", 96, 96,
    f'<rect x="{P}" y="{P-2}" width="72" height="72" rx="16" fill="#FFF7C2" stroke="{YEL_D}" stroke-width="4" filter="url(#sh)"/>', D, [24, 24, 24, 24], "Grid slot under the cursor.")
add("panels/slot_grid_empty.svg", 96, 96,
    f'<rect x="{P}" y="{P-2}" width="72" height="72" rx="16" fill="rgba(255,255,255,0.45)" stroke="rgba(255,255,255,0.9)" stroke-width="3" stroke-dasharray="8 6"/>', "", [24, 24, 24, 24], "Empty grid slot.")
add("panels/party_card.svg", 300, 140,
    f'<defs><linearGradient id="pc" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#F4FFE9"/><stop offset="1" stop-color="#D9F7C2"/></linearGradient></defs>'
    f'<rect x="{P}" y="{P-2}" width="276" height="116" rx="22" fill="url(#pc)" stroke="#BDE89F" stroke-width="3" filter="url(#sh)"/>',
    D, [36, 36, 36, 36], "Party member card (light green). 9-slice.")
add("panels/party_card_selected.svg", 300, 140,
    f'<rect x="{P}" y="{P-2}" width="276" height="116" rx="22" fill="#FFF7C2" stroke="{YEL_D}" stroke-width="4" filter="url(#sh)"/>',
    D, [36, 36, 36, 36], "Party member card under the cursor. 9-slice.")

# ---- cursor --------------------------------------------------------------
add("cursor/cursor.svg", 44, 48,
    f'<path d="M10 8 L36 24 L10 40 Z" fill="{WHITE}" stroke="#9AA3B0" stroke-width="3" stroke-linejoin="round" filter="url(#sh)"/>',
    shadow_filter(dy=2, blur=2, op=.3), None, "Selection cursor (points right). Sits left of the selected row.")
add("cursor/cursor_small.svg", 30, 32,
    f'<path d="M7 6 L24 16 L7 26 Z" fill="{WHITE}" stroke="#9AA3B0" stroke-width="2.5" stroke-linejoin="round"/>', "", None, "Small cursor for dense lists.")
add("cursor/value_arrow_left.svg", 32, 40, f'<path d="M22 6L8 20l14 14" fill="none" stroke="{MUTED}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>', "", None, "Value-change arrow, left.")
add("cursor/value_arrow_right.svg", 32, 40, f'<path d="M10 6l14 14-14 14" fill="none" stroke="{MUTED}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>', "", None, "Value-change arrow, right.")
add("cursor/scrollbar_track.svg", 16, 120, f'<rect x="4" y="2" width="8" height="116" rx="4" fill="#E3E9F1"/>', "", [10, 0, 10, 0], "Scrollbar track. 3-slice vertical.")
add("cursor/scrollbar_thumb.svg", 16, 48, f'<rect x="3" y="2" width="10" height="44" rx="5" fill="{MUTED}"/>', "", [8, 0, 8, 0], "Scrollbar thumb. 3-slice vertical.")

# ---- controller glyphs ---------------------------------------------------
def glyph_circle(letter, color):
    return (f'<circle cx="24" cy="24" r="21" fill="{color}" stroke="{WHITE}" stroke-width="3"/>'
            f'<text x="24" y="31.5" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="22" fill="{WHITE}">{letter}</text>')


for letter, color in [("A", "#F7A12B"), ("B", "#EE5A5A"), ("X", "#4A90E2"), ("Y", "#4CC46A")]:
    add(f"buttons/button_{letter.lower()}.svg", 48, 48, glyph_circle(letter, color), "", None, f"{letter} button glyph.")
for label, w in [("L", 56), ("R", 56), ("ZL", 64), ("ZR", 64), ("LB", 64), ("RB", 64), ("LT", 64), ("RT", 64)]:
    add(f"buttons/button_{label.lower()}.svg", w, 40,
        f'<rect x="2" y="2" width="{w-4}" height="36" rx="12" fill="{SLATE}" stroke="{WHITE}" stroke-width="3"/>'
        f'<text x="{w/2}" y="27" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="18" fill="{WHITE}">{label}</text>', "", None, f"{label} shoulder button glyph.")
for name, sym in [("plus", "M24 14v20M14 24h20"), ("minus", "M14 24h20")]:
    add(f"buttons/button_{name}.svg", 48, 48, f'<circle cx="24" cy="24" r="21" fill="{SLATE}" stroke="{WHITE}" stroke-width="3"/><path d="{sym}" stroke="{WHITE}" stroke-width="5" stroke-linecap="round"/>', "", None, f"{name} button glyph.")
add("buttons/button_dpad.svg", 48, 48,
    f'<path d="M18 4h12v14h14v12H30v14H18V30H4V18h14z" fill="{SLATE}" stroke="{WHITE}" stroke-width="3" stroke-linejoin="round"/>', "", None, "D-pad glyph.")
for d, path in [("up", "M24 8l-6 8h12z"), ("down", "M24 40l-6-8h12z"), ("left", "M8 24l8-6v12z"), ("right", "M40 24l-8-6v12z")]:
    add(f"buttons/button_dpad_{d}.svg", 48, 48,
        f'<path d="M18 4h12v14h14v12H30v14H18V30H4V18h14z" fill="{SLATE}" stroke="{WHITE}" stroke-width="3" stroke-linejoin="round"/><path d="{path}" fill="{YEL}"/>', "", None, f"D-pad, {d} highlighted.")
for s in ["l", "r"]:
    add(f"buttons/stick_{s}.svg", 48, 48,
        f'<circle cx="24" cy="24" r="21" fill="{SLATE}" stroke="{WHITE}" stroke-width="3"/><circle cx="24" cy="24" r="12" fill="none" stroke="{WHITE}" stroke-width="2.5" opacity=".6"/>'
        f'<text x="24" y="30" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="16" fill="{WHITE}">{s.upper()}</text>', "", None, f"{s.upper()} stick glyph.")

# ---- menu discs ----------------------------------------------------------
MENU = [
    ("dex", "#F0505A", "dex"), ("bag", "#F79A2B", "bag"), ("party", "#4FC64A", "party"),
    ("communicate", "#22B9CF", "chat"), ("save", "#8E6BE8", "save"), ("options", "#8C96A6", "cog"),
    ("map", "#3E8EF0", "map"), ("online", "#17A9D8", "wifi"), ("profile", "#F25C8A", "card"),
    ("controls", "#6C7A92", "gamepad"), ("resume", "#F2B705", "play"), ("states", "#5965D8", "layers"),
    ("friends", "#2FB58F", "user_plus"), ("pokegear", "#E86A3A", "phone"), ("box", "#3BA7A0", "box"),
    ("trainer", "#D9822B", "card"), ("quit", "#9AA3B0", "exit"), ("help", "#7C8BD9", "question"),
]


def lighten(hexc, f):
    hexc = hexc.lstrip("#"); r, g, b = (int(hexc[i:i+2], 16) for i in (0, 2, 4))
    r = int(r + (255 - r) * f); g = int(g + (255 - g) * f); b = int(b + (255 - b) * f)
    return f"#{r:02X}{g:02X}{b:02X}"


def disc(color, icon, size=128, selected=False):
    pad = 18 if selected else 10
    W = size + 2 * pad; c = W / 2; r = size / 2 - 4
    grad = (f'<radialGradient id="g" cx="40%" cy="30%" r="80%"><stop offset="0" stop-color="{lighten(color,.35)}"/>'
            f'<stop offset="1" stop-color="{color}"/></radialGradient>')
    ring = ""
    if selected:
        ring = (f'<circle cx="{c}" cy="{c}" r="{r+12}" fill="none" stroke="{YEL}" stroke-width="7"/>'
                f'<circle cx="{c}" cy="{c}" r="{r+12}" fill="none" stroke="#FFF7B0" stroke-width="2"/>')
    s = size * 0.46 / 48
    body = (ring + f'<g filter="url(#sh)"><circle cx="{c}" cy="{c}" r="{r}" fill="url(#g)" stroke="{WHITE}" stroke-width="6"/></g>'
            f'<g transform="translate({c - 24*s},{c - 24*s}) scale({s})">{ICONS[icon](WHITE)}</g>')
    return W, body, grad + shadow_filter(dy=5, blur=5, op=.3)


for name, color, icon in MENU:
    W, body, defs = disc(color, icon)
    add(f"menu/menu_{name}.svg", W, W, body, defs, None, f"Menu disc: {name}.")
    W, body, defs = disc(color, icon, selected=True)
    add(f"menu/menu_{name}_selected.svg", W, W, body, defs, None, f"Menu disc: {name}, selected.")

for name, color, icon in [("fight", "#F2557E", "fight"), ("pokemon", "#4FC64A", "party"), ("bag", "#F5B321", "bag"), ("run", "#3E8EF0", "run")]:
    W, body, defs = disc(color, icon, size=150)
    add(f"battle/command_{name}.svg", W, W, body, defs, None, f"Battle command disc: {name}.")
    W, body, defs = disc(color, icon, size=150, selected=True)
    add(f"battle/command_{name}_selected.svg", W, W, body, defs, None, f"Battle command disc: {name}, selected.")

# ---- bars and meters -----------------------------------------------------
add("bars/hp_frame.svg", 260, 36,
    f'<rect x="2" y="4" width="256" height="28" rx="14" fill="#2F3542" stroke="{WHITE}" stroke-width="3"/>'
    f'<rect x="6" y="8" width="44" height="20" rx="10" fill="{TEAL}"/>'
    f'<text x="28" y="24" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="14" fill="{WHITE}">HP</text>',
    "", [0, 20, 0, 60], "HP bar frame with the HP tag. Fill sits at x=54..252, y=10..26.")
add("bars/bar_frame.svg", 220, 28,
    f'<rect x="2" y="2" width="216" height="24" rx="12" fill="#2F3542" stroke="{WHITE}" stroke-width="3"/>', "", [0, 14, 0, 14], "Plain bar frame (PP, timer, progress). 3-slice.")
for name, a, b in [("green", "#8EF07A", "#3DC64B"), ("yellow", "#FFE680", "#F2C200"), ("red", "#FF8A8A", "#E5404A"),
                   ("blue", "#8CC8FF", "#3E8EF0"), ("teal", "#8FF0F0", "#1FB5B8"), ("purple", "#C9B5FF", "#8E6BE8")]:
    add(f"bars/fill_{name}.svg", 200, 16,
        f'<defs><linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{a}"/><stop offset="1" stop-color="{b}"/></linearGradient></defs>'
        f'<rect x="0" y="0" width="200" height="16" rx="8" fill="url(#f)"/><rect x="6" y="3" width="188" height="3" rx="1.5" fill="#FFFFFF" opacity=".45"/>',
        "", [0, 8, 0, 8], f"Bar fill, {name}. Stretch or crop to the value; 3-slice.")
for name, cols in [("good", ["#4CAF50"] * 3), ("fair", ["#F2A33A", "#F2A33A", "#3A4A6A"]),
                   ("poor", ["#E05252", "#3A4A6A", "#3A4A6A"]), ("lost", ["#3A4A6A"] * 3)]:
    add(f"bars/signal_{name}.svg", 40, 32,
        "".join(f'<rect x="{2+i*14}" y="{28-h}" width="10" height="{h}" fill="{c}"/>' for i, (h, c) in enumerate(zip([12, 20, 28], cols))),
        "", None, f"Signal bars, {name} (kept as before).")


def hexagon(r, cx=170, cy=170):
    return [(cx + r * math.cos(math.radians(-90 + 60 * i)), cy + r * math.sin(math.radians(-90 + 60 * i))) for i in range(6)]


hexbody = f'<polygon points="{poly(hexagon(150))}" fill="#F3FFF0" stroke="#BDE89F" stroke-width="5"/>'
for rr in (100, 50):
    hexbody += f'<polygon points="{poly(hexagon(rr))}" fill="none" stroke="#D3EFC2" stroke-width="2.5"/>'
for (x, y) in hexagon(150):
    hexbody += f'<line x1="170" y1="170" x2="{x:.1f}" y2="{y:.1f}" stroke="#D3EFC2" stroke-width="2.5"/>'
add("stats/hex_frame.svg", 340, 340, hexbody, "", None, "Stat hexagon frame: HP top, then Attack, Defense, Speed, Sp. Def, Sp. Atk clockwise.")
vals = [0.8, 0.7, 0.55, 0.9, 0.6, 0.65]
pts = [(170 + 150 * v * math.cos(math.radians(-90 + 60 * i)), 170 + 150 * v * math.sin(math.radians(-90 + 60 * i))) for i, v in enumerate(vals)]
add("stats/hex_fill_example.svg", 340, 340, f'<polygon points="{poly(pts)}" fill="#7AD957" fill-opacity=".75" stroke="#3DA63A" stroke-width="4" stroke-linejoin="round"/>',
    "", None, "Example stat fill. Draw the polygon from real stats at runtime.")

# ---- people --------------------------------------------------------------
def crown(fill, dark, light):
    return (f'<path d="M8 40 L5 14 L17 24 L28 8 L39 24 L51 14 L48 40 Z" fill="{fill}" stroke="{WHITE}" stroke-width="4" stroke-linejoin="round"/>'
            f'<rect x="8" y="40" width="40" height="8" rx="3" fill="{dark}" stroke="{WHITE}" stroke-width="3"/>'
            f'<circle cx="28" cy="29" r="5" fill="{light}"/><circle cx="5" cy="14" r="3.5" fill="{fill}" stroke="{WHITE}" stroke-width="2"/>'
            f'<circle cx="28" cy="8" r="3.5" fill="{fill}" stroke="{WHITE}" stroke-width="2"/><circle cx="51" cy="14" r="3.5" fill="{fill}" stroke="{WHITE}" stroke-width="2"/>')


for name, f, d, l in [("gold", "#FFC928", "#E09A00", "#FF6F7A"), ("silver", "#D6DCE5", "#9AA3B0", "#6FB7FF"),
                      ("bronze", "#E8A06A", "#B5683A", "#8FE38A"), ("teal", "#5FE0D8", "#1C9CA3", "#FFE04A"), ("red", "#FF6A72", "#C8323C", "#FFE04A")]:
    add(f"people/crown_{name}.svg", 56, 52, crown(f, d, l), "", None, f"Crown, {name}. Gold marks the host.")
for name, col in list(PLAYER.items()) + [("grey", "#A89880")]:
    add(f"people/chip_ring_{name}.svg", 96, 96,
        f'<circle cx="48" cy="46" r="38" fill="{WHITE}" stroke="{col}" stroke-width="8" filter="url(#sh)"/>', shadow_filter(dy=3, blur=3), None,
        f"Portrait ring, {name} player colour. Portrait 64 px inside.")
add("people/chip_empty.svg", 96, 96,
    f'<circle cx="48" cy="46" r="38" fill="rgba(255,255,255,0.4)" stroke="{WHITE}" stroke-width="5" stroke-dasharray="10 8"/>'
    f'<path d="M48 32v28M34 46h28" stroke="{WHITE}" stroke-width="6" stroke-linecap="round"/>', "", None, "Empty player slot.")
for name, col in [("online", "#4CD964"), ("busy", "#F0505A"), ("away", "#FFC928"), ("offline", "#A8B0BC")]:
    add(f"people/status_{name}.svg", 28, 28, f'<circle cx="14" cy="14" r="11" fill="{col}" stroke="{WHITE}" stroke-width="4"/>', "", None, f"Status dot, {name}.")
add("people/new_tag.svg", 64, 32,
    f'<rect x="2" y="2" width="60" height="28" rx="14" fill="{RED}" stroke="{WHITE}" stroke-width="3"/>'
    f'<text x="32" y="22" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="15" fill="{WHITE}">NEW</text>', "", None, "NEW tag.")
for name, col in PLAYER.items():
    add(f"people/name_pill_{name}.svg", 160, 56,
        f'<rect x="4" y="4" width="152" height="44" rx="22" fill="{col}" stroke="{WHITE}" stroke-width="4" filter="url(#sh)"/>',
        shadow_filter(dy=3, blur=3), [26, 26, 26, 26], f"Name pill, {name} player. 9-slice; text white.")
add("people/pointer.svg", 64, 64,
    f'<circle cx="32" cy="32" r="26" fill="{WHITE}" stroke="#E3E9F1" stroke-width="3" filter="url(#sh)"/><path d="M24 18l16 14-16 14z" fill="{INK}"/>',
    shadow_filter(dy=3, blur=3), None, "Off-screen friend pointer. Rotate toward the friend; tint the arrow with their colour.")

# ---- emotes --------------------------------------------------------------
BUB = (f'<path d="M36 6c16 0 28 10 28 24S52 54 36 54c-3 0-6 0-9-1l-11 9 2-13C11 45 8 38 8 30 8 16 20 6 36 6z" '
       f'fill="{WHITE}" stroke="#C9D2DE" stroke-width="3" stroke-linejoin="round" filter="url(#sh)"/>')
EMO = {
    "exclaim": f'<path d="M36 16v18" stroke="{RED}" stroke-width="8" stroke-linecap="round"/><circle cx="36" cy="43" r="4.5" fill="{RED}"/>',
    "question": f'<path d="M28 23a8 8 0 1 1 11 7c-2 1-3 3-3 5" fill="none" stroke="{BLUE}" stroke-width="6" stroke-linecap="round"/><circle cx="36" cy="44" r="4" fill="{BLUE}"/>',
    "heart": f'<path d="M36 45s-15-9-15-18a8 8 0 0 1 15-4 8 8 0 0 1 15 4c0 9-15 18-15 18z" fill="{PINK}"/>',
    "sweat": f'<path d="M30 18c-6 9-9 13-9 17a9 9 0 0 0 18 0c0-4-3-8-9-17z" fill="#5AB8F5"/><path d="M45 22c-3 5-5 7-5 9a5 5 0 0 0 10 0c0-2-2-4-5-9z" fill="#5AB8F5"/>',
    "sleep": f'<path d="M24 20h14L24 38h14" fill="none" stroke="{BLUE}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/><path d="M42 16h8l-8 10h8" fill="none" stroke="{BLUE}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>',
    "music": f'<path d="M30 42V20l16-4v20" fill="none" stroke="{PURPLE}" stroke-width="5" stroke-linejoin="round"/><circle cx="26" cy="42" r="5" fill="{PURPLE}"/><circle cx="42" cy="38" r="5" fill="{PURPLE}"/>',
    "hi": f'<text x="36" y="38" text-anchor="middle" font-family="{FONT}" font-weight="700" font-size="20" fill="#F2A33A">Hi!</text>',
    "angry": f'<path d="M26 20l4 7M46 20l-4 7M26 40l4-7M46 40l-4-7M24 30h8M40 30h8" stroke="{RED}" stroke-width="5" stroke-linecap="round"/>',
    "dots": f'<circle cx="24" cy="30" r="4.5" fill="{MUTED}"/><circle cx="36" cy="30" r="4.5" fill="{MUTED}"/><circle cx="48" cy="30" r="4.5" fill="{MUTED}"/>',
    "sparkle": f'<polygon points="{poly(star_pts(36,30,16,5,4,-90))}" fill="#FFC928"/>',
    "battle": f'<polygon points="{poly(star_pts(36,30,17,7,8,-22.5))}" fill="{RED}"/>',
    "thanks": f'<path d="M24 34l8 8 16-18" fill="none" stroke="{GREEN_D}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
}
for name, inner in EMO.items():
    add(f"emotes/emote_{name}.svg", 72, 72, BUB + inner, shadow_filter(dy=3, blur=3), None, f"Emote bubble: {name}.")

# ---- backgrounds ---------------------------------------------------------
def bg(name, c1, c2, circles, desc, stripes=None):
    defs = f'<linearGradient id="b" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{c1}"/><stop offset="1" stop-color="{c2}"/></linearGradient>'
    body = f'<rect width="1920" height="1080" fill="url(#b)"/>'
    if stripes:
        defs += (f'<pattern id="st" width="60" height="60" patternUnits="userSpaceOnUse" patternTransform="rotate(35)">'
                 f'<rect width="30" height="60" fill="{stripes}"/></pattern>')
        body += '<rect width="1920" height="1080" fill="url(#st)"/>'
    for (x, y, r, o) in circles:
        body += f'<circle cx="{x}" cy="{y}" r="{r}" fill="#FFFFFF" opacity="{o}"/>'
    add(f"backgrounds/bg_{name}.svg", 1920, 1080, body, defs, None, desc)


CIRC = [(220, 160, 300, .18), (1650, 260, 380, .14), (1180, 900, 420, .12), (420, 860, 200, .16), (980, 120, 120, .2)]
bg("sunny", "#FFF27A", "#C8F06A", CIRC, "Menu backdrop: yellow to lime, soft circles.")
bg("aqua", "#9BF0F5", "#2BB8D6", CIRC, "Pokédex / box backdrop: aqua.")
bg("lime", "#D9F78A", "#8EDC4A", CIRC[:3], "Settings backdrop: lime with stripes.", stripes="rgba(255,255,255,0.12)")
bg("lavender", "#EFE6FF", "#B9A6F2", CIRC, "Online / profile backdrop: lavender.")
bg("coral", "#FFD9C2", "#FF9A8A", CIRC, "Battle-menu backdrop: coral.")
bg("dusk", "#3A4A8C", "#1C2A44", [(c[0], c[1], c[2], c[3] * .5) for c in CIRC], "Dark backdrop for title / loading.")

# ---- render --------------------------------------------------------------
manifest = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path="/opt/pw-browsers/chromium-1194/chrome-linux/chrome") if os.path.exists("/opt/pw-browsers/chromium-1194/chrome-linux/chrome") else p.chromium.launch()
    for scale in (1, 2):
        page = b.new_page(device_scale_factor=scale, viewport={"width": 2000, "height": 1200})
        for it in items:
            base = os.path.join(OUT, it["path"][:-4])
            os.makedirs(os.path.dirname(base), exist_ok=True)
            if scale == 1:
                open(base + ".svg", "w").write(it["svg"])
            page.set_content(f'<html><body style="margin:0;background:transparent">{it["svg"]}</body></html>')
            el = page.query_selector("svg")
            el.screenshot(path=base + ("@2x.png" if scale == 2 else ".png"), omit_background=True)
        page.close()
    b.close()

for it in items:
    manifest.append({"name": os.path.basename(it["path"])[:-4], "folder": os.path.dirname(it["path"]),
                     "files": [it["path"], it["path"][:-4] + ".png", it["path"][:-4] + "@2x.png"],
                     "size": [it["w"], it["h"]], "nine_slice": it["slice"], "description": it["desc"]})
json.dump({"note": "Sizes are 1x pixels for a 1920x1080 screen; @2x files are double. nine_slice = [top, right, bottom, left] insets in 1x pixels.",
           "palette": {"ink": INK, "muted": MUTED, "line": LINE, "lavender": LAV, "yellow": YEL, "yellow_dark": YEL_D, "teal": TEAL,
                       "green": GREEN, "red": RED, "orange": ORANGE, "blue": BLUE, "purple": PURPLE, "pink": PINK, "navy": NAVY, "players": PLAYER},
           "sprites": manifest}, open(os.path.join(OUT, "manifest.json"), "w"), indent=1, ensure_ascii=False)
print(len(items), "sprites")
