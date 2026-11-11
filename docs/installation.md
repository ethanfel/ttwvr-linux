# Installation record and remaining work

The local workspace keeps the original downloads, MO2, conversion output,
backups, and runtime logs outside this Git repository.

## Layout

```text
workspace/
  TTWVR.zip                    original user-supplied archive
  downloads/                   original third-party archives
  local/
    MO2/                       portable Mod Organizer 2
      mods/                    individually staged mods
    TTW-Installer/             original Windows installer + TTW 3.4 MPI
    TTW_Linux_Installer/       upstream native installer source checkout
    root-mods/                 staged xNVSE and Vorbis libraries
    patched-root/              patched EXE, original backup, upstream patcher
    vr-state/                  created only when the VR wrapper is run
  ttwvr-linux/                 this repository, original tooling and records only
```

## 4GB patch reproduction

Obtain **FNV4GB for Proton 1.5** from
[the original patcher page](https://www.nexusmods.com/newvegas/mods/62552?tab=files).
Copy the original `FalloutNV.exe` to an empty staging directory, extract
`FalloutNVPatcher` beside it, make the patcher executable, and run it from that
directory. It creates `FalloutNV_backup.exe` and patches the copy in place.

The observed input and output hashes are in [fnv4gb-result.json](fnv4gb-result.json).
This is the author's existing patch, not an original patch developed here. The
original executable must be backed up before the staged output is deployed.

## Native TTW conversion

[SulfurNitride's native installer](https://github.com/SulfurNitride/TTW_Linux_Installer)
supports TTW MPI packages. The inspected source commit is
`936b0aef1f8379662c85aad639510f356dd9a0db` (version 0.2.0). Its README notes that
its output is approximately 18 GB rather than the Windows installer's 17 GB.
Size alone is not a sufficient validation: check asset/BSA failures and required
masters too.

The CLI documented upstream is:

```sh
mpi_installer install \
  --mpi '/path/to/Tale of Two Wastelands 3.4.mpi' \
  --fo3 '/path/to/Fallout 3 goty' \
  --fnv '/path/to/Fallout New Vegas' \
  --dest '/path/to/MO2/mods/Tale of Two Wastelands'
```

Conversion assets are generated. The native CLI was built from the inspected upstream
commit, without source changes. Its 525 registry dependencies were downloaded
separately, verified against Cargo.lock, and supplied as a local vendored source
for an offline release build. This repository does not bundle those dependencies
or the resulting binary.

The conversion produced 18,609,522,654 bytes. It reported 196,915 successful asset
operations and two failures, both attempts to copy Vorbis DLLs into the game
root. Those two default files were already superseded by the separately deployed
newer official Vorbis update; the installed update hashes were verified. The
nonzero converter exit is preserved in the result record rather than hidden.
All 18 masters have readable TES4 headers and a valid dependency order; all 28
generated BSA headers have version 104 and nonzero file counts. These checks do
not replace an in-game test.

## Prepared MO2 profiles

`tools/create_profiles.py` initializes a new portable instance from ordered JSON
lists of staged mods and plugins and from launcher-generated INIs. It refuses to
overwrite an existing instance or either target profile. The profile names are
`TTW - Flat` and `TTW - VR`, with Flat selected first. Only the latter enables
TTWVR, which has the final asset override priority. Profiles have separate saves
and INIs. MO2's default automatic archive invalidation is enabled and the per-
profile setting is left unset so MO2 performs the actual initialization.

MO2's INI must use `gameName=TTW`, the plugin's internal `gameName()` value.
`Tale of Two Wastelands` is its display title and is not accepted here. The first
launch exposed this configuration error; the local INI and generator were
corrected. See the [upstream game plugin](https://github.com/ModOrganizer2/modorganizer-game_ttw/blob/master/src/gamefalloutttw.cpp).

## Remaining setup

- The official guide utility set and presets have been staged and enabled in
  both profiles. ML Utilities Checker passed in game and is now disabled.
- Root files are deployed and their backups and checksums are recorded locally.
- MO2 initialized automatic archive invalidation in Flat. A new game reached
  the TTW intro scene, confirmed by the user. The
  copied guide custom INI includes TTW's starting quest and disables the NV intro.
- Select the VR profile and test the WiVRn launch path. Keep results distinct
  from settings that have only been prepared on disk.

No binary compatibility change should be applied without a reproducible runtime
failure. Record any future change with its upstream version, before/after hashes,
reason, validation, and rollback.
