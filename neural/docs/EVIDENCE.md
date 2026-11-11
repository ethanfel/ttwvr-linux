# Isolated validation, 2026-11-11

This records a working **spatial NR prototype**, not completed temporal NR, DLAA,
or a headset comfort/performance validation. All screenshots below were checked
after the game loaded Vault 101, beyond the main menu. Raw evidence is retained
locally rather than published with game assets or identifying save filenames.

## Scene discovery

- TTWVR 1.1.0 SHA-256:
  `8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880`.
- Verified borrowed D3D9 device getter at module RVA `0x4f5770`, accessing
  `module+0x19710e8`. SHA-256, relocated instruction bytes and owning window PID
  are checked before installing hooks. No on-disk TTWVR patch.
- A previous temporary-device discovery attempt replaced TTWVR's cached device
  pointer and crashed. Removed; no second D3D9 device is created in the game.
- Baseline OpenXR frames 2010/2011: both eyes 2944×2208 BGRA8 sRGB; 2454/2439
  D3D9 events, no overflow. No submitted OpenXR depth/motion extension.
- Scene-input frame 570: full-size RGBA16F HDR and D24S8 depth for each pass,
  quarter-size RGBA16F bloom and 1×1 adapted RGB luminance.
- Final tone-map PS bytecode FNV-1a hash `18030291181409727554`: bloom s0, HDR s1.
  Bound s2 is not sampled by that shader. Adapted luminance RGB sum travels in
  bloom alpha. `c1.x=1.3`; measured bloom alpha 0.828125/0.8271484375; effective
  exposure multiplier 1.0 for each eye. Sampler sRGB reads and writes were off.
- Pass-0 LDR mean byte error against XR left/right: 1.852/21.408. Pass-1:
  21.516/1.787. This and image inspection support pass 0 → left, pass 1 → right;
  they are not pixel-identical because later presentation processing remains.
- D24 capture values fit the low 24 bits, normalize by 16777215, and visually
  match scene geometry in both eyes. Observed depth ranges approximately
  0.87984–0.99604 and 0.88943–1.0; not reversed depth.
- Unused shader constants can contain NaN. The initial diagnostic wrote invalid
  JSON; fixed to serialize nonfinite values as null. Used exposure constants were
  finite. The reader normalizes only the old diagnostic's NaN literals.

## GPU transport and replay

- 32→64→32 D3D11 keyed shared HDR probe: 4096 pixels in each direction,
  zero mismatches on stock Proton. No cross-bitness shared fence imported.
- D3D9↔D3D11 HDR: 100 alternating round trips, 819200 pixels verified,
  zero mismatches.
- Raw INTZ depth export failed `OpenSharedResource` with `0x80070057` because
  DXVK does not publish compatible depth metadata. Replaced by GPU-only D24
  image→device-local buffer→RGBA8 bytes and shader decoding. Standalone packed
  depth probe passed 4096 pixels with zero mismatches. A later version verified
  four depth quadrants (0.1234567, 0.375, 0.9876543, 1.0) with zero mismatches.
- One standalone probe launch reported a DXVK submission timeout/device loss
  during device initialization. A fresh isolated-prefix run of the same binary
  passed all 100 HDR round trips and the four-quadrant depth check. Its cause is
  not established; initialization reliability remains an open test item.
- Native-size captured-HDR identity replay, both eyes: no nonfinite output,
  zero alpha changes, every value within two FP16 ULPs of its original. Small
  rounding changes are expected through encode/decode. Signed HDR is retained.
- Real NR captured-HDR replay, both eyes: `neural_evaluate output_applied=1`,
  approximately 19.3 million changed components per eye, no nonfinite values,
  zero alpha changes. NR preset B, intensity 0.5, native 2944×2208 model input.
  Motion zero and reset=true on every evaluation; no temporal quality claim.

## Live in-game output

- Identity-only control ran for thousands of frames; frames 2400/2401 captured
  both eyes. Identity logs use `conversion_evaluate`, not `neural_evaluate`.
- Actual NR host created independent features for eyes 0 and 1 and evaluated
  consecutive frames with `output_applied=1`. The scene hook then copied the
  processed HDR back before the game's original tone mapping.
- Actual NR OpenXR capture frames **630 and 631**, both eyes, confirmed
  `neural_rendering=true`, `mode=spatial_nr_reset_every_frame`, `dlaa=false`,
  rect `[0,0,2944,2208]`. Stereo previews show loaded Vault 101 gameplay.
- Native-resolution NR added cost. Median wall interval per application frame,
  measured over 120-frame log intervals: identity 26.82 ms (27 intervals), NR
  43.05 ms (30 intervals), roughly 37 vs 23 FPS in this simulated test. These
  include synchronization/CPU work and are not isolated neural GPU timings.
  Capture intervals can stall; these are diagnostic observations, not a controlled
  headset benchmark. Full NR handoff samples were about 5–6 ms per eye.
- The offline off/on comparison uses exactly the same scene input. Live control
  and NR runs share the save/location but NPC motion differs. Do not present them
  as identical-frame comparisons. Consecutive screenshots do not establish
  flicker/ghosting/comfort; those remain open.

## Preservation and limits

The test used copied game executables, MO2, prefix, profile and saves, with the
original game/MO2/prefix and Titanfall paths mounted read-only. All eight original
save files matched their preparation hashes afterward. The isolated game was
closed after capture and its NR setting returned to disabled. WiVRn's default
registration and the working full-resolution Steam path were unchanged.

Only the loaded Vault 101 scene, this TTWVR binary and this resolution were
validated. Menus, scene transitions, device recreation, resizing and mixed-eye
failure recovery require additional work. Shader identities are verified locally;
raw proprietary shader code is not distributed. DLAA activation is rejected.
