# Change log

## 2026-11-11 — Quest 3 gameplay and private OpenXR bridge patch

- Connected TTWVR's direct OpenXR backend to WiVRn 26.9 / Meta Quest 3 at 90 Hz.
  The user saw the menu, then a black headset view after loading the save.
- Reproduced Proton's mixed-layer allocation bug: projection-first arrays
  reserve only 48 bytes per layer and corrupt subsequent 112-byte quads.
- Created a hash-guarded two-byte workaround in a private `wineopenxr.so` copy.
  Both conversion paths reserve 120 bytes for projection-first arrays.
- Added an actual-binary regression harness: the original bridge corrupts the
  first quad in both architectures; the private patch passes all four checks.
- The wrapper overlays the verified file for VR launches through bubblewrap.
  The system's original Proton checksum is unchanged. Eight launcher tests
  pass, including refusing a changed Proton build and ignoring this patch in Flat.
- Verified the private library is loaded in the game's mount namespace. The
  user confirmed visible headset gameplay after loading, and the invalid-pose
  errors disappeared. Performance tuning and full controls testing remain.
- [Exact scope, hashes, commands, evidence, and rollback](docs/openxr-layers.md).

## 2026-11-11 — Simulated VR save load and shader compiler workaround

- Backed up the user's flat save and copied it into the separate VR profile.
- Initialized TTWVR 1.1.0's direct OpenXR backend under Proton 11 WoW64 with
  the original Monado simulated HMD; observed active head tracking and its menu.
- Reproduced a save-load crash in Wine's built-in D3DCompiler 47 / libvkd3d.
  Installed checksum-verified native D3DCompiler 47 in this game's prefix,
  retaining original DLL backups. With a process-local native override, that
  save loaded and the VR spectator view rendered Vault 101. The game later
  logged a normal player-requested exit.
- Added optional per-instance `ttwvr-linux.ini` settings for WoW64 and native
  D3DCompiler 47 in VR mode. Seven wrapper tests pass, including preservation
  of other DLL overrides and leaving flat launch settings alone.
- Recorded the remaining custom-DXVK, DLSS, and blood-depth-copy limitations.
  An explicit `bUseDXVK=1` experiment did not change renderer selection and was
  reverted. No TTWVR binary patch or NVTF change was applied.
- [Evidence, download hashes, application and rollback](docs/simulated-vr.md).
  Physical WiVRn headset output and controls remain untested.

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
- Backed up the existing game-root files and deployed the patched executable,
  xNVSE, and updated Vorbis libraries. Verified all eight deployed files by
  SHA-256. The game filesystem does not support all metadata preservation;
  the reported copy warnings did not affect verified file contents.
- Built the native TTW installer from upstream commit
  `936b0aef1f8379662c85aad639510f356dd9a0db`, without source changes. Verified
  525 dependency archives against Cargo.lock and compiled offline. The installer
  accepted both games and parsed 196,917 operations in the TTW 3.4 package.
- Completed the TTW conversion assets: 18,609,522,654 bytes, 18 ESMs and 28 BSAs.
  The converter exited nonzero for two game-root Vorbis copies blocked by its
  restricted process. Both are superseded by the separately installed and
  checksum-verified newer official Vorbis files. All 196,915 other asset
  operations succeeded. [Detailed result](docs/ttw-conversion-result.json).
- Deployed and verified xNVSE's configuration file (nine root/data files total).
- Confirmed a normal Steam New Vegas launch with the user. The runtime log shows
  xNVSE 6.4.9 initialized, reached `NewGameHook`, and returned from the game
  normally. No MO2 plugins were loaded in this run; TTW remains untested in game.
- Created `TTW - Flat` and `TTW - VR` portable MO2 profiles from launcher-generated
  INIs, the official guide custom INI, and a dependency-checked 18-master load
  order. The VR asset mod has the final override priority and is disabled in Flat.
- First Steam wrapper launch opened MO2 2.5.2. Fixed the generated `gameName`
  value from the display title to the plugin's internal identifier `TTW` after
  MO2 rejected the display title. The installed `game_ttw.dll` loaded correctly;
  this was a configuration error, not a missing plugin. Backed up the local INI
  and corrected the profile generator to prevent recurrence.
- On retry, MO2 recognized the TTW game plugin. It generated the two missing
  optional GECK editor INIs; the gameplay INIs were already present.
- Fixed host `XR_RUNTIME_JSON` leaking into Steam's container and overriding
  its imported runtime manifest. The wrapper clears the variable only after
  the container entry point, before Proton. Live OpenXR validation is pending.
- Added ShowOff 1.84 and Crash Logger 5.17 from their authors' GitHub releases,
  checked release SHA-256 digests, and enabled both profiles. The next game log
  confirmed both DLLs loaded successfully. [Checksums](docs/added-dependencies.json).
- Staged and enabled the remaining user-downloaded guide utilities and presets.
  ML Utilities Checker was enabled in Flat only for validation.
  [Archive manifest](docs/guide-dependencies.json).
- The user confirmed the in-game **All utilities installed!** message. This
  validates the checker-covered plugins, LAA patch and custom INI setting.
  Disabled the checker in both profiles after the game and MO2 closed.
- The user confirmed a new game reached TTW's intro scene in the Flat profile;
  the xNVSE log also records `NewGameHook`. This is an initial gameplay check,
  not a full playthrough or save/load validation.
- Fixed the flat-profile startup waiting for a disconnected VR runtime. Auto
  mode now selects an empty private OpenVR registry and `DXVK_NO_VR=1` for
  `TTW - Flat`; game and MO2 startup were subsequently observed. Five launcher
  subprocess tests pass, including profile selection and no VR requirement.
- Retested the user's original Monado runtime after unrestricted desktop access
  became available: native OpenXR initialization passed, reporting the simulated
  HMD, two 640x720 views, and result 0. No rebuild or global runtime change needed.
- The same native preflight also passed inside Steam Linux Runtime 4 using the
  imported OpenXR manifest, including a run with simulated left/right WMR
  controllers. TTWVR frame submission is still untested.
- Added optional Proton 11 WoW64 selection to the launch wrapper; this remains
  an unverified VR compatibility candidate.
- Located the user's existing simulated Monado runtime. Recorded the restricted
  process's USB-discovery failure and the separate simulation-only build's Vulkan
  failure; neither is a TTWVR runtime result. See the compatibility notes.
- Verified the wrapper with a controlled subprocess: command arguments survive
  paths containing spaces; ambiguous commands are rejected; dry-run creates no
  files; runtime selection and its private registry reach the launched process.
- Inspected the supplied VR DLL's imports and embedded diagnostics.
- Added a repeatable read-only audit and a Steam/MO2 launch wrapper.

Not yet done:

- Extended TTW gameplay and save/load validation.
- Live Proton/WiVRn test, headset rendering, controls, and save/load verification.
- Any binary or source compatibility patch to TTWVR/DXVK/Proton/xrizer.

Downloaded archives remain unchanged. Changed original game-root files are
backed up under the local workspace's `local/backups/fnv-root-before-install/`.
