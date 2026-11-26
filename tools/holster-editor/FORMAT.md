# Holster state format and validation

This editor supports the placement formats in TTWVR 1.1.5 through 1.1.8. It does not infer format support from a file extension or change a
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

## Head bindings and equipment spots in 1.1.8

A numbered holster in `[SavedSlots]` can have a binding such as `head 1 0.21 -0.14 -0.31`. The three metre offsets are forward, right and up from the neck. Mirroring negates the right offset. In older editor exports, a bone and head record can coexist for the same placement. The game uses the valid bone first. Editing the binding writes one explicit record or removes both for automatic anchoring.

The ammo pouch and Stimpak circle each have a named position in `[SavedSpots]` with a forward/right/up fallback and grab radius in metres. Either spot can have a bone or head binding. With only a position record, the spot uses its body-relative coordinates.

```ini
[SavedSpots]
1=spot pouch 0.18 -0.12 -0.50 0.078
2=bone pouch right_forearm 0.13 -0.04 0.08
3=spot stim 0.18 0.12 -0.50 0.128
4=head stim 0.18 0.09 -0.21
```

The editor recognises either spot independently. On an older file, the user can explicitly create the new section from the legacy pouch. A named placement uses an explicit 5–45 cm radius. When creating one without a saved radius, the editor starts at 15 cm.

The stock 1.1.8 DLL loaded an editor-exported head holster, forearm ammo pouch and head-following Stimpak circle in an isolated Wine prefix. All three bindings, offsets and radii matched. The game’s writer then saved the placements, the editor read them back and an unedited export matched those bytes exactly.

## Ammo pouch in 1.1.7

TTWVR 1.1.7 stores the ammo pouch separately in `[SavedPouch]`. Its four
body-relative values are forward, right, below-eye and above-feet in metres.
The optional `anchor` record contains a torso bone ID (0 pelvis, 1 spine,
2 spine1, 3 spine2), three translations in skeleton units and nine orientation
values. The reference preview uses 70 skeleton units per metre. Moving the pouch
converts the displacement to bone-local translation and applies the same move to
the fallback, subtracting upward movement from below-eye distance. Reanchoring between torso bones converts the saved orientation and translation into the
new bone frame. Choosing body-relative writes the preview position into the fallback
and removes the anchor. A newly created attachment starts upright in the reference pose.
The 1.1.7 pouch reader accepts radius values from 0.05 to 0.45 metres inclusive.
Removing the radius record restores TTWVR’s configured radius. Untouched numeric
tokens keep their original spelling. Files with only the
four-value position use above-feet height for the preview.

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

## Front/back correction, Field Kit 1.4

Field Kit 1.1–1.3 used the supplied skeleton's world Y as the editor's **back**
coordinate. This was incorrect: the skeleton is right/**forward**/up. Both toes
extend roughly 11.63 skeleton units in positive Y from the feet. The corrected
editor applies `B = diag(1,-1,1)` to reference origins and rotation rows after
orthonormalizing the native frames. The resulting preview matrices are orthogonal
with determinant -1; transpose remains their inverse. Local offsets do not change
merely by loading a file. Sideways mirroring still reflects local Z.

[Independent direction fixture](tests/direction-fixture.json) records both foot/toe
positions from the original skeleton and one gameplay pelvis rotation with relative
foot-to-toe vectors. Across 194 body-ready samples from the reported session, the
horizontal toe heading agrees with pelvis local +Y: minimum dot 0.8661, median
0.9975, maximum nearly 1. The old front pouch's local Y was -0.2493891 m, placing
it behind the pelvis. This evidence establishes the direction, independently of
the editor's projection math. It does not establish comfort or every live pose.

**Repair old body placement** is explicit and undoable. For pelvis/chest/leg
slots whose corrected preview centre has positive back, reflect that centre's
back coordinate, then transform it into the same bone's local frame. This retains
the old intended preview position and handles tilted bones and nonzero origins;
it is not a local-Y sign flip. Arms and Automatic slots are untouched. Radii,
enabled/visibility flags, fallback positions and other state remain unchanged.
A repaired centre cannot be repaired back to the rear. No automatic migration is
performed because state files do not record which editor version produced them,
and deliberately rear-mounted holsters must remain valid.

Regression coverage includes independent direction evidence, actual old belt/thigh
offsets, tilted chest/shin repair, arm preservation, repeat safety, UTF export,
and browser download/undo/redo. The earlier loader readback below verifies the
state format, not the old preview's incorrect direction. Physical fit after this
correction still needs a headset check.

## Gear-style preset profiles, Field Kit 1.4

`src/presets.js` defines target centres in reference right/back/up centimetres.
The waist layouts use four points at 1.00 m reference height: front pouches at
`right = ±0.095 m, back = -0.19 m`, and hip holsters at
`right = ±0.25 m, back = -0.075 m`. All four use the pelvis anchor. The default
adds two rows of chest-frame pockets at reference heights 1.35 m and 1.17 m.
This fills the belt and torso while leaving the arms free.

Paired right targets are converted to bone-local offsets via `G.local`; the
left partner uses the verified mirror transform. Extra forward spacing is
applied in the reference frame before conversion. Paired slot order can be
reversed independently of single-target placement. Utility rig has a single
centre pouch at slot 7 and one forearm target at slot 8, opposite the chosen
Pip-Boy arm. Right is reserved by default. No preset has two targets on one arm.
The Pip-Boy selector affects preset placement only, not the actual wrist setting.

A single forward plane is inappropriate for real side belt holsters. Regression
checks instead use a conservative reference torso cylinder with elliptical
cross-section (right radius 0.18 m, depth radius 0.115 m, centred at back -0.01 m,
height interval 0.90–1.48 m), plus capsule limbs following the reference joints.
Every preset sphere clears those proxy volumes by over 1 cm across both paired
slot orders, all three forward-spacing choices and both Pip-Boy sides. The
minimum sphere-to-sphere gap is at least 5 cm. These are reference checks only;
they do not establish live avatar collision, clothing clearance or ergonomic
comfort. The targets must still be fitted in the headset.

Apply replaces anchors, offsets, radii and enabled flags in chosen slots. Others
are disabled with their placements retained. Saved fallbacks, global visibility,
calibration and unrelated state remain unchanged. No item binding is inferred.

Validation: 43 Node tests, including nine profile tests and 84 preset variants.
Checks cover belt arc/height, torso usage, sphere spacing, proxy body clearance,
pair order, independent Pip-Boy reservation, input immutability and UTF-8/UTF-16
exports. Chromium checks preview/cancel, actual utility-profile downloads with
left forearm and no right forearm, changing Pip-Boy side, side-view grouping,
undo/redo and mobile overflow, alongside all existing editor tests.

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
Automatic markers use the saved forward/right/up offsets from eye level. The
editor reads a positive finite `[SavedHeight] Head` in metres when available,
otherwise it uses a nominal 1.67 m eye height. Front and side drags update the
saved position and preserve automatic binding.

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
