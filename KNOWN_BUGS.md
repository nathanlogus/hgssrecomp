# Known bugs and limits

Check this list before you report a problem. It is written from the changelog, the README
and the project's history, so it only lists what is documented there. If something is not
here, or is listed as fixed in a version newer than yours, please report it. In the game,
open the pause menu, GAME tab, **Report a bug** (or press F9). It saves a folder
(`reports`, beside your save) with the latest messages, your settings and a picture of
the game. It never contains your save, the ROM or chat. Attach the files to the issue.

"Fixed in" names the first release with the fix; "Unreleased" means the next one.

## Screens redrawn (NEW SCREENS)

- Some questions and messages inside redrawn screens are still the game's own: what to do
  with an item, how many, and the messages (bag); the clerk's questions before and after
  buying or selling, and the seal shops (shops); the Poke Ball part of a trade.
- In the screen layouts with both screens, X is still the game's own menu. Only the
  single-screen view has the pause menu's version.
- Later parts of the game, and some screens (radio, contests, the Pokeathlon, wireless
  features), were never fully checked and may show a generic two-screen layout.
- Full screens were cut off on displays narrower than 16:9 (4:3, 16:10).
  Fixed in: Unreleased.

## Widescreen and the picture

- Anything the game hides by other means than the box test could pop in at the far left
  and right. None found so far.
- No edge-marking outlines in the widescreen picture.
- 2D weather and overlays do not extend beyond the 4:3 area.
- World detail above 720 lines costs frame time on slower machines.
- Washed-out colours on Windows with some graphics cards. Fixed in: v0.2.4.
- The game was always at night (the DS clock started at midnight on every launch).
  Fixed in: v0.2.4.

## Online and the Plaza

- The Plaza's games, Tap Toys and parade are not there yet.
- The menu cannot be opened in the Plaza (as in the original game), so you cannot save
  there; the receptionist saves on the way in.
- The DS's wireless (Union Room, link battles, trades) is relayed through the party
  server. The maintainer's tests found this works only up to about 10 ms round trip, so
  play on the same network or close by; slower connections lag or fail.

## Battles

- Tried on 16:9 only; wider phone screens (20:9) are untested in battles.
- A move effect crossing the gap between the two Pokemon can look stretched there for a
  moment.
- The battle strip vanished for a moment between choosing FIGHT and the moves
  appearing. Fixed in: v0.2.5.

## Controllers

- Only the first controller found was used. Fixed in: v0.2.6.
- On Steam Deck, a controller that does nothing may need Proton Experimental as the
  shortcut's compatibility tool (reported, cause not found).

## Android

- Not yet tried on many real phones or handhelds; it was tested in Google's emulator
  (Android 14). Reports welcome, with `log.txt`.
- The app cannot update itself: it only tells you that a newer version exists.

## Platforms and building

- Windows, macOS and ARM Linux have had less testing than x86 Linux. macOS is
  community-maintained.
- A first build takes 20 to 40 minutes and a lot of memory.
- The recompiler failed when its output folder was on another drive. Fixed in: v0.2.1.
