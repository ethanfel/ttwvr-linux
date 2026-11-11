# Change log

## 2026-11-11 — Initial investigation

Completed:

- Confirmed both English Steam installations contain all required DLC masters.
- Recorded the supplied TTWVR 1.1.0 archive hash and relevant PE architectures.
- Downloaded and extracted MO2 2.5.2 and xNVSE 6.4.9 into local staging folders.
- Downloaded JohnnyGuitar NVSE 5.28.
- Staged TTWVR 1.1.0, YUPTTW 13.9.1, TTW NVSE 3.3.3b, JIP LN 57.30,
  JIP's settings, JohnnyGuitar, Stewie Tweaks 10.05, its VNV preset, and
  Engine Optimizations 1.0 in the portable MO2 mods directory.
- Extracted TTW 3.4's conversion package and the updated Vorbis root files.
- Applied the author's FNV4GB for Proton 1.5 patcher to a staged copy of the
  original executable. Verified LAA enabled, unchanged file length, and 188
  differing bytes. [Checksums and deployment status](docs/fnv4gb-result.json).
- Verified the wrapper with a controlled subprocess: command arguments survive
  paths containing spaces; ambiguous commands are rejected; dry-run creates no
  files; runtime selection and its private registry reach the launched process.
- Inspected the supplied VR DLL's imports and embedded diagnostics.
- Added a repeatable read-only audit and a Steam/MO2 launch wrapper.

Not yet done:

- TTW conversion and installation of the remaining guide dependencies.
- MO2 profile configuration and deployment of root files into New Vegas.
- Live Proton/WiVRn test, headset rendering, controls, and save/load verification.
- Any binary or source compatibility patch to TTWVR/DXVK/Proton/xrizer.

The original game installation and downloaded archives have not been modified.
The patched executable exists only in local staging so far.

