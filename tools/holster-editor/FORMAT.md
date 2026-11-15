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
