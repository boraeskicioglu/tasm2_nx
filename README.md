<div align="center">

<img src="launcher/icon.jpg" alt="The Amazing Spider-Man 2" width="180">

# tasm2_nx

**The Amazing Spider-Man 2 on Nintendo Switch**

An unofficial Nintendo Switch wrapper/port of Gameloft's Android game.

[![Switch](https://img.shields.io/badge/Nintendo_Switch-Homebrew-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)](#installation)
[![Version](https://img.shields.io/badge/version-v1.0.0-4C8BF5?style=for-the-badge)](#)
[![Android](https://img.shields.io/badge/Android-1.2.8d-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#about)
[![AArch32](https://img.shields.io/badge/AArch32-32--bit-0091BD?style=for-the-badge&logo=arm&logoColor=white)](#building)

</div>

---

## About

`tasm2_nx` runs the original 32-bit Android version of **The Amazing Spider-Man 2** on Nintendo Switch, using the open-source [android32](https://github.com/aks796/android32) runtime.

The supported game build is:

```text
Package:      com.gameloft.android.ANMP.GloftASHM
Version:      1.2.8d
Version code: 12723
ABI:          armeabi-v7a / AArch32
```

The original ARM game library runs directly inside a 32-bit Horizon OS process. The wrapper supplies the Android/Bionic/JNI, graphics, audio, input and filesystem interfaces it expects. Players must provide their own APK and expansion files; these are not included in the repository or release download.

**Status:** gameplay, local saves, settings and the offline costume shop have been tested on a real Switch. The latest gameplay build was reported working normally after the saved-profile startup fix. Performance is playable at medium detail, with a **60 FPS target rather than a constant 60 FPS guarantee**. Busy scenes and streaming can still cause drops or stutters.

## Features

- Original Android **1.2.8d** game running in AArch32 on Switch.
- **1280 × 720** widescreen presentation with configurable internal render scale.
- 60 FPS frame-cap patch and corrected slow-motion/finisher timing.
- Switch controller input, camera controls and native touchscreen support.
- A menu cursor for navigating the original touch-oriented interface.
- Fully local equipment/costume shop with **seven additional costumes available offline**.
- Persistent native checkpoint saves and settings.
- Music, voices and sound effects through android32 OpenSL ES / Switch audio output.
- Direct OBB access with bounded caching; expansion archives stay on the SD card.
- Startup loading display, load-time performance handling and crash/debug logs.
- A 64-bit NRO launcher carrying the 32-bit payload, with sphaira HOME-menu forwarder installation and updates.

## Installation

### Requirements

- A Nintendo Switch running Atmosphère homebrew.
- [sphaira](https://github.com/ITotalJustice/sphaira) for installing the HOME-menu forwarder.
- Your own supported **1.2.8d ARMv7 APK**.
- Both required OBB expansion files listed below.

Download the release ZIP and copy its `switch/` folder to the root of your SD card. Add your APK and OBB files so the layout is:

```text
sd:/switch/tasm2_nx/
├── tasm2_nx.nro
├── The-Amazing-Spider-Man-2.apk
├── main.12032.com.gameloft.android.ANMP.GloftASHM.obb
└── patch.12723.com.gameloft.android.ANMP.GloftASHM.obb
```

| Required expansion | Exact size |
| --- | ---: |
| `main.12032.com.gameloft.android.ANMP.GloftASHM.obb` | 1,236,510,228 bytes |
| `patch.12723.com.gameloft.android.ANMP.GloftASHM.obb` | 98,576,965 bytes |

The APK may have another filename: setup searches for the supported package and version. It checks the ARMv7 libraries against the audited native variant. Repacked libraries, another native build, or an x86-only APK are unsupported.

The optional older `patch.12438.com.gameloft.android.ANMP.GloftASHM.obb` can be mapped when present; it does not replace either required expansion. OBBs may also be stored under the port's `obb/` or `Android/obb/com.gameloft.android.ANMP.GloftASHM/` directory.

### First launch

1. Open **sphaira** and find **The Amazing Spider-Man 2** in Homebrew.
2. Choose **Install Forwarder**.
3. Start the new icon from the Switch **HOME menu**.
4. Allow the launcher to install its 32-bit payload and restart into the game.
5. Wait for first-time setup and the loading screen to finish.

The game runs through its HOME-menu forwarder. The NRO opened directly in ordinary homebrew mode shows installation instructions.

First setup extracts the native libraries and generates the local runtime files. The OBBs remain intact and are read directly; they are not extracted on every launch. Keep the APK in the installation folder for its original assets and signing certificate.

### Updating

Close the game and replace `tasm2_nx.nro` **at the same path**, then launch the HOME-menu icon again. The launcher updates the embedded 32-bit payload when a newer gameplay build is detected.

Keep `data/`, `gameloft/`, `sdcard/`, `config.ini`, the APK and OBBs. Updates do not require resetting your profile. Existing HOME-menu forwarders can retain their old artwork; recreate the forwarder in sphaira if you want it to use the new NRO icon too.

## Controls

### Gameplay

| Nintendo Switch | Input |
| --- | --- |
| **Left Stick** | Move |
| **Right Stick** | Camera |
| **B / A / Y / X** | Gamepad A / B / X / Y actions; follow the game's action prompts |
| **ZL / ZR** | Left / right trigger |
| **L / R** | Left / right shoulder |
| **D-Pad** | Native gamepad directions |
| **+ / −** | Start / Select |
| **L3 / R3** | Stick clicks |
| **Touchscreen** | Original mobile controls and touch prompts |

### Menus

| Nintendo Switch | Action |
| --- | --- |
| **Left Stick / D-Pad** | Move the yellow menu cursor |
| **A** | Select / tap |
| **B** | Back |
| **R3** | Recenter the cursor |
| **− + R3** | Toggle the cursor manually |
| **Touchscreen** | Native touch input |

The cursor hides during gameplay and appears for menus. `[controls] swap_ab` in `config.ini` swaps the bottom/right face buttons during gameplay; menu A/B remain consistent.

## Performance and configuration

`config.ini` is created automatically beside the NRO. **Medium** in-game detail is the tested baseline for the current release. Actual frame rate depends on the scene; stable 60 FPS is not claimed.

| Option | Default | Purpose |
| --- | --- | --- |
| `[performance] render_scale` | `0.80` | 1024 × 576 base internal render targets; output and touch coordinates stay 1280 × 720 |
| `[performance] boost_cpu_when_loading` | `true` | android32 startup and long-load CPU boost handling |
| `[controls] deadzone` | `0.18` | Analog-stick deadzone |
| `[controls] swap_ab` | `false` | Swap gameplay bottom/right face buttons |

Set `render_scale=1.00` for full internal resolution, or `0.75` for a 960 × 540 base and lower pixel cost. Restart the game after changing the scale. Loading boosts end before normal presentation and are idled while the game is unfocused.

The local costume shop lets you choose the seven additional suits without contacting obsolete Android services. Costume quantities/upgrades and the original game checkpoints are retained. Online purchases and currency purchases are unavailable.

## Save data and troubleshooting

The installation is self-contained under `sd:/switch/tasm2_nx/`:

```text
data/              Android internal storage and native saves
data/files/        Native ud_*.sav checkpoint/profile files
data/shared_prefs/ Settings and preferences
gameloft/          Gameloft shared-storage paths
sdcard/            Other Android shared-storage paths
config.ini         Port configuration
debug.log          Current startup/gameplay diagnostics
crash.log          Crash report when one occurs
```

The original game's checkpoint rules apply. Closing the game from HOME flushes complete checkpoints already queued by gameplay; it does not save an arbitrary position at any moment. Preserve the entire data folder when updating, moving or backing up the port.

If startup reports missing or unsupported files, check the APK version and exact OBB filenames/sizes first. If you need to report a problem, include the build number and relevant `debug.log`, `debug.prev.log` and `crash.log`, along with what you were doing and your graphics setting. Review logs for device identifiers before sharing them. Game files and personal saves should not be uploaded to GitHub issues.

## Building

The wrapper can be rebuilt without the APK or OBBs. Original game files are needed only for optional native-binary regression tests.

Requirements:

- Docker with the Linux container engine running.
- A shell environment such as Windows/MSYS2.
- Python 3 for the build tools and artifact checks.
- [android32](https://github.com/aks796/android32) in `runtime/`.
- An installed [libnx32](https://github.com/aks796/libnx32) prefix for AArch32.
- A compatible **32-bit softfp Mesa/Nouveau** stack from [mesa32](https://github.com/aks796/mesa32) in `portlibs32/`.

The build scripts use the AArch32 vita2hos toolchain container and the AArch64 devkitPro launcher container. Set `DCR_LIBNX32` to your installed libnx32 prefix if it is not in a standard sibling location. SDK archives are supplied separately.

For a checkout that keeps `runtime/` as a Git submodule, initialize it first:

```sh
git submodule update --init --recursive
```

The prepared upload folder and source release archive already include the pinned runtime source files.

From the project root:

```sh
./build.sh
./launcher/build.sh
python3 runtime/tools/gen_imports.py --port . --check
python3 tools/verify_artifacts.py
```

Final executable:

```text
launcher/tasm2_nx.nro
```

The root `tasm2_nx.nsp` is the wrapper's ExeFS payload, embedded automatically in the NRO. Use the NRO/forwarder installation flow above. `launcher/icon.jpg` contains the launcher artwork.

Host regressions run with `bash tools/test_host.sh` and require GCC/Python. Optional native ARM tests need Unicorn, pyelftools and a privately supplied supported library. See [VALIDATION.md](VALIDATION.md), [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) and [BUILD_REPORT.md](BUILD_REPORT.md) for validation details and dependency revisions.

## Known limitations

- Only the audited Android **1.2.8d / version code 12723 / ARMv7** native variant is supported.
- Gameplay targets 60 FPS, but busy scenes and resource streaming can run below that target.
- Online login, cloud saves, ads, Android updates and purchases are unavailable.
- Local progression follows the game's normal checkpoint rules.
- Obsolete Android notices are suppressed; ordinary gameplay confirmations remain.
- Testing covers the reported gameplay sessions, not every mission, texture, costume combination or long sleep cycle.

## Credits

- **Bora Eskicioğlu / BESK Entertainment** — Nintendo Switch port, game-specific compatibility, offline shop, save/input/audio fixes and release integration.
- [aks796/android32](https://github.com/aks796/android32) — core AArch32 Android runtime and launcher architecture.
- [aks796/libnx32](https://github.com/aks796/libnx32), [aks796/mesa32](https://github.com/aks796/mesa32), libnx, Mesa/Nouveau and devkitPro contributors — Switch SDK, graphics and toolchains.
- [aks796/a8retry_nx](https://github.com/aks796/a8retry_nx) — generic Java/preferences reference patterns.
- [NextOs-Ports/tasm2-nextos](https://github.com/NextOs-Ports/tasm2-nextos) — TASM2 Android compatibility reference.
- [cualquiercosa327/spiderman_vita](https://github.com/cualquiercosa327/spiderman_vita), [aks796/sonic_allstars_nx](https://github.com/aks796/sonic_allstars_nx) and [spiderman_total_mayhem_nx](https://github.com/boraeskicioglu/spiderman_total_mayhem_nx) — porting references.
- Gameloft and the original game's contributors for The Amazing Spider-Man 2.

## Legal

This unofficial homebrew compatibility project is not affiliated with or endorsed by Nintendo, Gameloft, Marvel or Sony. The original game's names, characters, artwork, trademarks and game data belong to their respective rights holders.

No APK, OBB, original native game library, gameplay data or player save is included. Players supply files from their own legally obtained game copy. The supplied launcher icon is game artwork and is not covered by the MIT license for original port code.

See [LICENSE](LICENSE), [THIRD_PARTY.md](THIRD_PARTY.md) and `licenses/` for the applicable code and dependency notices.
