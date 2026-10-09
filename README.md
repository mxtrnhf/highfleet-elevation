# Highfleet Manual Elevation

Lets you set the **elevation (floor) of any part** in the shipworks by hand, for **Steam 1.163**.

In vanilla the game decides each part's elevation with a hidden rule (roughly: a hull block rises one
level when every block connected to it is at the same height or higher, and mounted modules rise with
it). This mod adds hotkeys to override that for the part you are holding.

Elevation matters because it decides which parts can block which: a part only obstructs weapons and
engine exhaust of parts at the same height or lower, so with
[highfleet-qol](https://github.com/logdot/highfleet-qol)'s restored gun-arc blocking you can design
guns that fire over your own hull.

## Requirements
* Highfleet **Steam 1.163**. On any other build the mod refuses to start and writes why to its log.
* [Highfleet-Modloader](https://github.com/logdot/Highfleet-Modloader) installed.

## Install
1. Install the modloader if you have not (you should see `bass.dll` and `bass.dll_` in the game folder).
2. Extract this zip into the game folder (it merges into `Modloader/mods`).
3. Start the game. `Modloader/config/elevation.ini` is created on the first run.

## Use
In the shipworks, **pick up a part with the mouse and keep holding it**, then:

| Key | Action |
|-----|--------|
| `U` | raise the part's floor by one |
| `P` | lower the part's floor by one |
| `\` | give the part back to the game's automatic elevation (then **drop it** so the game recomputes) |

A green text readout in the top-left corner shows the held part, its floor, whether it is `manual` or
`auto`, and the floors of the parts attached to it.

Any part can be edited, not only hull blocks. Mounted modules (guns, sensors) follow their mount by
default; see `sync_children`. The game still recomputes other, automatic parts around a manual one, so
neighbours may rise or fall relative to it.

## Configure
`Modloader/config/elevation.ini` (restart the game after editing):

```ini
max_floor=3        # highest floor you can set
sync_children=1    # 1: parts mounted on the edited part move with it
child_offset=0     # floor of a mounted part relative to its mount (0 = same, 1 = one above)
overlay=1          # 1: show the text readout
```

## Sharing ships
A ship saved with manual elevation stores it in the normal ship file. It loads the same for players
**without** this mod, because the game already honours those saved values; the mod just gives you a
way to set them.

## Uninstall
Delete `Modloader/mods/highfleet_elevation.dll`. Ships you saved keep their elevation.

## Notes and limits
* The readout is a separate transparent window. It works in windowed and borderless mode and may not
  appear in exclusive fullscreen.
* Hotkeys are fixed (`U`, `P`, `\`). The `\` key sits in different places on non-US keyboards.
* Log file: `Modloader/elevation.log`.
* Developed and tested on Linux (Proton). It is a standard 64-bit Windows DLL and should work on
  Windows; reports are welcome.

## How it works
Each part keeps its elevation in `m_floor` (offset `+0x9b8`) and a bitmask `m_floor_type` (`+0x9b0`).
Bit `0x01` tells the game's solver to recompute the part. The mod clears that bit and writes `m_floor`
directly on the part currently held in the editor (the editor's held-part pointer at `0x1403c0d88`).
Findings and addresses are in `NOTES.md`.

## Building
```
make            # builds build/highfleet_elevation.dll (needs mingw-w64; on MSYS2 use: make WINCXX=g++)
make dist       # builds dist/highfleet_elevation-vX.Y.Z.zip
```

## Credits
Built on top of [Highfleet-Modloader](https://github.com/logdot/Highfleet-Modloader) by logdot.
Not affiliated with the developers of Highfleet.

Licensed under the MIT License (see `LICENSE`).
