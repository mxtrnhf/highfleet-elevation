# Highfleet 1.163 (Steam) reverse-engineering notes

Image base `0x140000000`. Static analysis is done on a live-process dump
(`/home/maksim/Development/hf163/Highfleet_1.163_dump.exe`, Ghidra program of the same name in
`/home/maksim/Development/hf_server/HighfleetServer.gpr`). The on-disk exe is Steam-DRM packed.
Code in the dump is byte-identical across dumps, so addresses are stable for this build.

Status legend: **V** = verified in the live game, **R** = read from decompilation only, **?** = guess.

## Where things overlap with prior public work
- logdot's ShipworksExtended: `DefinePartEx` 0x14021b190 (V, same address in 1.163), `CreateLibraryEx`
  0x14027f200, `MakeGlobalEx` 0x140194de0, `CopyStringEx` 0x1400056e0, `New` 0x140334094, editor
  libraries at 0x1403bd400.. (stride 0x18), `EditorParts` global 0x143a139f0, patch site 0x1400d0add.
- rentry guide: general RE primer for v1.151 (main, HGE statically linked, Steam DRM). No elevation/arc info.
- logdot's 1.151 Ghidra database: class skeletons only (Body has 31 members, ~3 named).
- Everything below about Body fields, elevation, arcs and the held-part pointer is new work.

## Mapped functions (1.163)
| Address | Name (in Ghidra) | Purpose | Status |
|---|---|---|---|
| 0x140025240 | Body::LoadField | per-key loader for `.seria` Body fields (was 0x140023c20 in 1.151) | R |
| 0x14004f010 | Body::SerializeProperties | writes every Body field; best field-offset map | R |
| 0x140256a90 | Ship_SolveFloors | elevation solver, sets `m_floor` from `m_floor_type` + neighbours | R, behaviour confirmed by manual test |
| 0x140052340 | Body::ScanSectors | 360 raycasts, fills `m_sectors`, calls UpdateEngineCovered; virtual (vtable-only callers) | R |
| 0x14001c4a0 | Body::UpdateEngineCovered | averages sector clearness in the engine cone, sets `m_mdl_covered` | R |
| 0x140239f30 | World_RaycastBodies | ray vs all bodies; floor rule below | R |
| 0x140281e00 | GetStats (ShipPart lookup; logdot calls it GetStats) | part-type descriptor by Body+0x188; global table 0x143a33bf0 | R |
| 0x14021b190 | EditorParts_DefinePart | clone part by oid into an editor library, sets m_count | V |
| 0x1400bc5e0 | Editor_OnMouseWheel | wheel rotates the held part, or scrolls palette | R |
| 0x1400bdad0 | Editor_RotateHeldPart | applies rotation using `m_ed_rotate` | R |
| 0x140164180 | (UI markup parser) | `obj_scale`, `obj_rotate`, `glossary`, `cancelok` etc. text tags; shows GUI button recipe | R |
| 0x14012a910 | (GUI element ctor, size 0xe70) | used by `cancelok` to build buttons | R |
| 0x1400bba30 | (Editor ctor stub) | logs "Editor created successful." | R |

## Globals
| Address | Meaning | Status |
|---|---|---|
| 0x1403c0d88 | held part `Body*` in the editor (null when nothing held) | V |
| 0x1403ef368 | editor-active flag | V |
| 0x1403ef369 / 0x1403ef36a | modifier-like flags used by the wheel handler | ? |
| 0x143a33bf0 / 0x143a33bf8 | global Body table begin/end | R |
| 0x143a33c90 / 0x143a51150 | list/count of bodies iterated by the raycast | R |

## Body field offsets (1.163)
| Offset | Field | Notes |
|---|---|---|
| +0x128 / +0x12c | m_position.x / .y | V (matches live part positions) |
| +0x138 | m_angle | |
| +0x190 | m_oid (std::string, SSO) | V; length at +0x1a0, cap at +0x1a8 |
| +0x1b0 | m_b2bstate | default 2 |
| +0x1d0 | m_density | |
| +0x1f0 | m_layer | int, default 1 |
| +0x2c8 / +0x2cc | m_health / m_health_max | |
| +0x350..+0x358 | slot vector (m_slots) | |
| +0x508 / +0x510 | m_sectors vector<float> begin/end | 360 entries after ScanSectors |
| +0x7a8 | m_count | |
| +0x848 | m_mdl_covered | float; set to 1.0 when engine cone is blocked |
| +0x84c | m_weapon_type | |
| +0x8d8 | m_stage | state-machine-like constants; NOT elevation |
| +0x8c0 | m_sprites | |
| +0x90c | m_z | float scale, default 1.0 |
| +0x9b0 | m_floor_type | V; bitmask, see below |
| +0x9b8 | m_floor | V; the elevation int |
| +0x9c4 | scratch flag used by the floor solver | R |
| +0xa28 | m_ed_rotate | float |

## Elevation rules (Ship_SolveFloors)
`m_floor_type` bits: `0x01` auto-recompute, `0x02` neighbours must be strictly higher,
`0x04`/`0x08` neighbour-count variants, `0x10` force floor 3. Parts with neither `0x01` nor `0x10` keep the
saved `m_floor` (manual test with `elev_test_manual.seria` confirmed). Auto parts rise by 1 when every
connection-slot neighbour is at >= their floor, repeating until stable; the rise also bumps attached
non-socket children by 1. Saved ships contain `m_floor` 1-3.

## Arc/obstruction rules
`Body::ScanSectors` casts 360 rays (1 degree each). A hit writes 0 into that degree's `m_sectors` slot, a
clear ray writes a weight from the owner descriptor. `World_RaycastBodies` lets another body block a ray only
if `param_7 != 0` (derived from flag bit 5 at descriptor +0xb0, inverted) or `shooterFloor <= otherFloor`
(+1 if the body's master type is 7). So elevation decides who can block whom.

## Open questions
- Where the gun-side firing check reads `m_sectors` and why it is not enforced in the release build.
- Whether `ScanSectors` is gated off by data (descriptor +0x128 == 0) or by a patched call.
- GUI button creation/click routing for real on-screen buttons (keyboard hotkeys are the first version).

## Project files
- Mod source: `/home/maksim/Development/hf_elevation/elevation.cpp` -> `elevation.dll`
  (hotkeys `[` `]` `\` on the held part; needs Highfleet-Modloader + `Modloader/mods/`).
- Test ship: `Ships/elev_test_manual.seria` (part A manual floor 2, part B forced floor 3).
- Assistant memory copy of key facts: `~/.claude/projects/.../memory/highfleet-163-reverse-engineering.md`.

## Prior work found later (read this before redoing anything)
- https://github.com/logdot/highfleet-qol (cloned to /home/maksim/Development/hf_qol_src/highfleet-qol) already
  restores gun firing-arc blocking in 1.163. `src/guns.rs`: detours `FireGun` (0x1400300d0 region, inject at
  0x140032f22, exit 0x140032ef0, return 0x140032f29), calls `is_gun_blocked(gun)` which reads the gun's
  360-float `m_sectors` (Body+0x508/0x510), requires descriptor (GetStats(Body+0x188)) +0x128 != 0, takes the
  gun's angle relative to the root body, maps to a 1-degree sector, blocked if value == 0.0.
  It is a per-angle check at fire time (better than the aggregate `m_mdl_covered` idea in this file).
- That repo also uses `patchy-rs` (trampolines) and `highfleet-rs` (struct definitions) from logdot; the
  latter is worth cloning for more field maps.
- The qol mod does not touch m_floor; arc blocking still depends on ScanSectors/World_RaycastBodies, which
  use the floor rule documented above, so manual elevation should change what blocks a gun.
