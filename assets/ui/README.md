# ui-sprites

These are the UI sprites for the ssrecomp online-mode HUD, drawn in the flat, bright style of Pokémon: Let's Go. Each sprite is its own file, so you can edit them one at a time.

## Layout

| Folder | What's in it |
|---|---|
| `panels/` | Cards, rows (normal / selected / disabled / teal / gold / red), dialogue box, yellow and aqua frames, buttons, tabs, toast, hint bar, code cells, session badge, box slots, party cards |
| `menu/` | 18 round menu discs, each with a `_selected` variant |
| `battle/` | FIGHT / POKéMON / BAG / RUN command discs, each with a `_selected` variant |
| `buttons/` | Controller glyphs: A B X Y, L R ZL ZR, LB RB LT RT, + −, D-pad (plus each direction), sticks |
| `icons/` | 38 line icons in ink colour; `icons/white/` holds the same set in white |
| `cursor/` | Selection cursor, small cursor, value arrows, scrollbar track and thumb |
| `bars/` | HP/bar frames, fills (green, yellow, red, blue, teal, purple), signal bars (good / fair / poor / lost) |
| `stats/` | Stat hexagon frame and an example fill |
| `people/` | Crowns, player chip rings, status dots, NEW tag, name pills per player colour, pointer |
| `emotes/` | Speech-bubble emotes: ! ? heart sweat sleep music hi angry … sparkle battle thanks |
| `backgrounds/` | 1920×1080 backdrops: sunny, aqua, lime, lavender, coral, dusk |

## Files per sprite

- `name.svg`: the source. Edit this one.
- `name.png`: 1×, sized for a 1920×1080 screen.
- `name@2x.png`: 2×, for 4K or for scaling down.

## manifest.json

`manifest.json` lists every sprite with:

- `size`: in 1× pixels
- `files`
- `description`
- `nine_slice`: `[top, right, bottom, left]` insets, given for every stretchable panel

For example, `panels/card` is `[36, 36, 36, 36]`. Keep the corners fixed, stretch the edges and centre, and you can draw it at any size. The manifest also holds the palette:

| Token | Hex |
|---|---|
| ink / muted | `#3B3F4A` / `#8A93A3` |
| line | `#DCE3EC` |
| yellow / rim | `#FFE04A` / `#E8B400` |
| lavender (dialogue) | `#B9A6F2` |
| teal (title band) | `#2EC4C6` |
| pink / purple / blue | `#F25C8A` / `#8E6BE8` / `#3E8EF0` |
| navy (session badge) | `#1C2A44` |
| players | `#3D6FD1` `#4CAF50` `#F2A33A` `#E05252` |

## Regenerating

`gen.py` builds the whole folder from code:

```
python3 gen.py <output-folder>
```

You need Python 3 and Playwright with Chromium installed, because the PNGs are rendered from the SVGs.

- To change a colour everywhere, change the palette at the top of `gen.py` and rerun it.
- If you only want to tweak one sprite, edit its SVG or PNG by hand.
- Rerunning `gen.py` overwrites hand edits, so keep a copy of anything you changed.

## Not in this folder

The Pokémon and item icons and the game's font are read from the player's own ROM when the game starts (`frontend/romart.h`, `frontend/hud.h`); nothing of the game is shipped. A font of the player's own can replace the letters (`assets/fonts`, not part of the project).
