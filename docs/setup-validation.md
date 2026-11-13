# Linux setup-tool validation — 2026-11-13

The tool was exercised against a disposable prepared-install fixture containing
real TTWVR 1.1.4 files, a copied initialized prefix and native Fluorine settings.
Paths deliberately contained spaces. The user's playable installation was
not used as the patch target.

## Installation and rollback

- `check` accepted supported original files and rejected an unknown plugin
  before creating an installation directory.
- `prepare` downloaded and verified both shader compilers, cloned the pinned
  DXVK revision and built all three DLLs with both source patches.
- `apply` created the expected combined 1.1.4 plugin, private overlays, selected
  mod configuration and launcher.
- A native Qt 6 test read the generated Wrapper options through **QSettings**
  and **QProcess::splitCommand**, verifying arguments with spaces exactly.
- The generated launcher and game wrapper passed dry-run checks with author
  NVAPI enabled and the archived neural layer disabled.
- Real bubblewrap mounts exposed the new renderer hashes while original Proton
  files remained unchanged outside the namespace.
- `rollback` restored the original INI, mod selection and compiler state. The
  original plugin and sentinel save stayed unchanged; activation files were
  removed. The private mod copy/build were retained as documented.
- **30 tests passed**, including real user-namespace/save-mount integration,
  symlink restoration, interrupted-transaction recovery, corrupted-backup
  rejection and preservation of conflicting edits before forced rollback.

## Fresh dependency build in gameplay

The freshly built DLLs have different full-file hashes from the earlier local
bundle. They were therefore tested in the existing isolated Monado gameplay
harness, using the 1.1.4 combined patch and its author helpers:

| Mode | Output per eye | Consecutive / translated frame pairs |
|---|---|---|
| DLSS Quality, 200% | 5888×4416 | 2370/2371 and 2550/2551 |
| Native Vulkan FSR Quality, 100% | 2944×2208 | 2310/2311 and 2490/2491 |

Both eyes contained fresh consecutive images and changed through the
reconstructed center after translation. Actual DLSS NGX evaluations alternated
features 3/4/3/4, all returning `0x1`. Native FSR was enabled, initialized and
active, with no failure or copy refusal. Stereo previews were visually
inspected. No custom NR/DLAA/SR/FG processing was active.

The existing harness still contained an old neural-menu test mod and used a
passive capture layer. That menu is **not installed by the public setup tool**;
the tool disables known addon/menu mods and rejects embedded conflicting files.
The user's separate author-renderer headset profile also has both addons disabled.

All 13 private baseline files were restored; 65 checked regular-install files
were unchanged. Both test games closed normally.

[Machine-readable build and validation record](evidence/linux-setup-20261113.json).

These are installer, launcher/mount and isolated GPU checks, not a clean-room
installation on another distribution or a new physical-headset validation.
The GPU harness used the existing private test manager; native Fluorine's
underlying launch/save-mount integration was separately validated and previously
confirmed in headset use. No sustained performance claim is made for this run.


## Compact configuration and guide revision

On 2026-11-13, the three-field workspace configuration was checked with **45 passing tests**, including real user-namespace/save-mount integration. New cases cover inherited Proton/prefix paths with spaces, enabled-mod discovery, ambiguous/disabled candidates, overrides, legacy absolute configurations, tilde expansion, missing preparation and path traversal.

A compact config for the existing isolated native CachyOS fixture resolved all nine required paths identically to its full-path config. This was a read-only resolution comparison; no new game launch or full installer deployment was needed for the configuration change. Existing apply/rollback evidence above still describes the installation path.

The guide's two screenshots were captured from native Fluorine 0.4.1 on an isolated virtual display; the test manager configuration was restored afterward. Shell/JSON examples, local links and all four Discord messages were checked. A complete fresh-install walkthrough remains untested, as stated in the preparation guide.
