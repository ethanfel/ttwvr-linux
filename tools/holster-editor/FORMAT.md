# Holster state format and validation

This editor targets the state format observed in the supplied TTWVR 1.1.5 and
1.1.6 builds. It does not infer format support from a file extension or change a
mod DLL. The 1.1.6 plugin hash and compatibility audit are recorded in
[the version audit](../../docs/ttwvr-1.1.6.md).

## Records

The INI section `[SavedSlots]` contains numbered text records. The number before
`=` is an entry index; the first value inside a position or bone record identifies
the actual holster (1 through 8). They are different numbers.

```ini
[SavedSlots]
1=1 0.2500 -0.2250 -0.3500 0.0650
2=2 0.2500 -0.0750 -0.3500 0.0650
; ...position records for slots 3 through 8...
9=bone 1 right_upper_arm 0.1000 0.0000 0.0000
```

Position record: `slot forward right up radius`, all four values in metres.
Bone record: `bone slot anchor localX localY localZ`, offsets in metres relative
to that bone. The mod converts these offsets to engine units using its world
scale. Local X/Y/Z are not labelled forward/right/up because those axes follow
the bone's orientation.

Supported names, in the mod's enumeration order:

| ID | Serialized name |
|---|---|
| 0 | `pelvis` |
| 1 | `chest` |
| 2 | `left_thigh` |
| 3 | `right_thigh` |
| 4 | `left_shin` |
| 5 | `right_shin` |
| 6 | `left_upper_arm` |
| 7 | `right_upper_arm` |
| 8 | `left_forearm` |
| 9 | `right_forearm` |

The editor's **Automatic** choice is not a serialized anchor name. It removes
the slot's bone record, retaining the position record. The loader marks that
slot for automatic binding when the avatar is available. Explicit bindings
retain the saved local offsets; they are not world-space positions.

```ini
[SavedSlotsOn]
1=on 1 1 0 1 1 0 0 0
2=show 1
```

The eight `on` values correspond to slots 1–8. Nonzero means enabled. `show`
controls the always-shown setting. Editing writes canonical 0/1 values.

## Verified behavior

The supplied 1.1.6 reader at RVA `0x21ffa0` accepts the record shapes above,
checks all ten anchor names, converts offsets using world units per metre, and
clamps grab radius to **0.05–0.45 m**. The state-section adapter at RVA `0xe15d0`
feeds stored record values to the line reader at `0xe1550`. These are private
interoperability observations; no upstream source or game binaries are included.

The editor preserves unmodified data and only rewrites changed values. Adding
or removing bone records also reindexes that section's numbered entries.
Comments and unknown extension payloads are retained. Duplicate known records,
unsupported anchors, incomplete slot data and non-finite numbers are rejected.
An existing radius outside the mod's range is preserved on an unedited round-trip;
any radius edit must fall within the verified range.

## Export precision fix, Field Kit 1.1.1

The old nine-significant-digit formatter could change `Math.fround(value)` when
rounding a double produced by dragging. A reproduced local offset was
`-0.0340234804687308`: writing `-0.0340234805` crossed a float32 midpoint and
correctly triggered the export guard. The serializer now uses the shortest
round-tripping decimal of the edited JavaScript value. It still preserves
unchanged record spellings and verifies exported bytes; verification was not
removed or relaxed.

Regression validation: 30 Node tests, including midpoint-adjacent values across
all numeric fields and 500 deterministic drag/mirror layouts; full Chromium
load/edit/download tests; and the actual previous Field Kit 1.1 HTML reproducing
the failure, then successfully exporting through the no-reload recovery script.
The latter retains the loaded input, current tab edits and unrelated slot data.
No additional game/DLL behavior changed.

## Reference geometry and mirroring

`src/geometry.js` contains small reference coordinate frames, not a model/mesh.
The preview uses the supplied 1.1.6 skeleton's parent-composed transforms at a
nominal 70 skeleton units/metre (roughly a 1.8 m figure). It does not read or
rewrite the player's height or arm calibration. Bone-local offsets and radii
remain in metres; front and side are orthographic projections at the same scale.
Automatic binding is not guessed: its fallback record can be edited/mirrored,
but a sphere preview requires an explicit anchor.

The mod transforms an explicit offset by the chosen bone rotation and adds the
bone origin. Body anchors use cached body nodes; arm anchors use the rendered
hand/forearm/upper-arm chain. The editor uses a fixed reference pose, so the
preview is approximate when those bones are tracked/animated in-game.

For a bone pair, let `S = diag(-1,1,1)` reflect the reference body's lateral axis
and `D = diag(1,1,-1)` reflect local Z. The supplied skeleton satisfies
`R_left ≈ S R_right D` and `origin_left ≈ S origin_right`. Pelvis and chest obey
the same relation with themselves as the target. Therefore mirror uses
`[localX, localY, -localZ]`, **not a guessed local-X sign flip**, plus the opposite
anchor name. The saved forward/right/up fallback uses `[forward, -right, up]`.
Radius and enabled state copy unchanged. Global visibility is not changed.

The check uses both sides from the original skeleton, independently of the
editor's symmetric reference frame construction. Maximum matrix residual was
below `6e-6`; maximum origin residual was below `0.000084` skeleton units.
The supplied upper-body mesh's arm frames also matched. NIF layout was read
using the [NifTools schema](https://github.com/niftools/nifxml/blob/develop/nif.xml).
[Input hash and frame checks](reference-check.json). No NIF, texture, mesh, or
user configuration is redistributed. This verifies the reference-frame relation;
it does not prove symmetry for arbitrary live poses, custom skeleton replacements,
or physical comfort in a headset.

## Placement and mirror validation, 2026-11-15

- Six geometry tests: orthonormal/invertible frames, mirrored world positions
  for all ten anchors, upper-arm direction, front/side drag axes, automatic
  fallback handling, and mirrored state serialization without source mutation.
- Chromium: actual pointer drags in both views, drag radius, live numeric preview,
  keyboard movement, cancelled drags, one-step undo, source/destination mirror
  values in downloaded state files,
  mirror undo/redo, and existing preservation/error/responsive checks.
- The existing 1.1.6 loader test below remains evidence for the unchanged state
  format. New physical placement and headset mirror checks have not been run.

## Validation, 2026-11-15

- 21 Node tests: exact byte round-trips across line endings and UTF encodings;
  unrelated section/comment preservation; anchor addition/removal; entry indices
  versus slot IDs; all ten anchors; radius/offset edits; flags; malformed and
  duplicate input; float32 precision; missing final newline.
- Chromium integration: actual file loading and downloading, exact no-op export,
  map and keyboard selection, centimetre conversion, toggles, numeric errors and
  unfinished edits, undo/redo/reset, rejected-file retention, sample mode,
  themes, help, responsive layouts, zero network requests and no JS exceptions.
- Actual TTWVR 1.1.6 loader in a private Fluorine/Proton/Monado session: loaded an
  edited state copy and a gameplay save, then read back the two changed bone
  bindings, metre-scaled offsets, radii and enabled state. Values matched the
  editor's output. The private game closed and all six test configuration files
  were restored; the four protected normal-install files were unchanged.

[Game-loader readback](validation.json). This validates serialization and runtime
loading. It does not establish comfortable physical placement, fix drift, or
constitute a Windows OS/browser test. No user state file is included in the tests.
