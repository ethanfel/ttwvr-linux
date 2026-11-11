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

Conversion has not yet run. The native binary is obtained separately; this
repository does not bundle it. A source-build attempt could not fetch crates
from the restricted build environment and was stopped before compilation.

## Remaining setup

- Complete conversion, check its log, masters and output size.
- Complete the official guide utility set and create flat/VR MO2 profiles.
- Back up and deploy the root files (xNVSE, the patched executable, Vorbis).
- Generate/verify game INIs, TTW's starting quest, archive invalidation and load
  order; launch flat TTW first.
- Select the VR profile and test the WiVRn launch path. Keep results distinct
  from settings that have only been prepared on disk.

No binary compatibility change should be applied without a reproducible runtime
failure. Record any future change with its upstream version, before/after hashes,
reason, validation, and rollback.
