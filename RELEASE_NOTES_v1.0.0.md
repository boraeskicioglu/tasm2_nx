# tasm2_nx 1.0.0

Built first-release candidate for the original ARMv7 The Amazing Spider-Man 2 Android 1.2.8d / code12723. Metadata: Bora Eskicioğlu / BESK Entertainment; template program ID `055A3A4744DC9001`.

- Standard android32 64-bit NRO launcher with sphaira forwarder install/update and embedded AArch32 wrapper.
- Manifest/ABI/native variant checks, one-time native library setup and direct discovery of required main12032/patch12723 OBBs; optional patch12438 mapping.
- Explicit Java/local service replacements, original APK assets/certificate, offline obsolete services and original owner-supplied catalog metadata.
- Exported game lifecycle, GLES2/Mesa 1280 × 720 vsync, conservative NV fences and verified startup modal/destructor fixes.
- Native gamepad events, camera/movement axes, multi-touch and menu cursor; input releases on pause/disconnect.
- android32 OpenSL ES → audout with queue recovery and callback lifetime handling; persistent preferences and native save flush monitoring.
- Bounded diagnostics, crash reporting and FastLoad termination after first successful presentation.

The user reports the saved-profile startup fix in build `202610051919` is
working normally, with no new observed issues. Medium-detail gameplay is
playable; stable 60 FPS in every scene is not claimed. The public NRO carries
the supplied Spider-Man artwork, with the same tested gameplay payload.
Online/cloud/purchases remain unavailable. See [VALIDATION.md](VALIDATION.md).

The archive contains the launcher and documentation/license notices only. Players supply their own APK and OBBs. Exact output sizes, build number and hashes are in [BUILD_REPORT.md](BUILD_REPORT.md).

## 2026-10-06 public release preparation

- User-reported console acceptance of the saved-profile startup fix.
- Supplied Spider-Man artwork replaces the placeholder NRO icon.
- Public README covers installation, both required OBBs, controller/menu input,
  offline costume choices, save preservation and realistic performance limits.
- Original gameplay payload/build stamp retained; no new gameplay patches.
- Prepared repository sources and separate GitHub Release assets exclude APKs,
  OBBs, native game libraries, personal saves, logs and private analysis files.

## 2026-10-05 saved-profile startup crash fix (build 202610051919)

All four supplied crash entries resolve to `strcmp` called by the wrapper's
costume-unlock hook, with the same invalid address `0x268db39d`. The hook had
incorrectly interpreted inventory value `+0x14` as a name pointer. It now uses
the original native exact numeric-key lookup, requiring all seven costume
entries before any grant. Positive owned/available quantities remain intact.

The extended ARM Shop regression reproduces the old crash with build 1816 and
passes with the new payload. It checks native identity/ownership/availability
accessors, a fresh-address inventory map, 1,200 later ticks, retained quantities
and no grants against partial/absent inventory. Native UserData dual-file codec
and reload regressions also pass. Full mission resume after a console reboot
still requires hardware verification.

Replace only `tasm2_nx.nro` at the same installed path. Retain all generated
profile/data folders, config, APK and OBBs. There is no save migration or reset.
Build 1816 and its documents/binaries are archived in `release/archive/`.

## 2026-10-05 medium-detail performance update

The user confirms the previous offline Shop and progression fixes work on build
1703. Its medium-detail log varies roughly 29–60 FPS. This candidate targets
higher and more consistent frame rates with:

- Native render-target scale 0.80 (1024x576 base, 36% fewer pixels), retaining
  medium game detail and 1280x720 presentation/input coordinates. A new
  `[performance] render_scale` option is appended automatically; 1.00 restores
  full resolution, 0.75 is available for a larger pixel-load reduction.
- Original ordinary RAM allocation instead of redundant 64 MiB SD mmap arenas.
- Indexed OBB cache hits, with existing bounded memory and I/O correctness.
- Existing android32 loading boost watcher, explicit frame/focus/exit boundaries,
  and low-frequency CPU/boost diagnostics.

Host/native regressions and both cross-builds pass, including native allocation,
render dimensions, Shop, saves and finisher. Steady 60 FPS and scaled rendering
on this candidate still need console measurement. Replace only the NRO at the
same path and retain `data/`, `gameloft/`, `sdcard/`, APK and OBBs. The working
1703 build is retained in the source project's `release/archive/`.

## 2026-10-05 offline progression persistence

The user reports settings surviving relaunch, but mission progress disappearing after closing software from HOME. Most native UserData constructors enable cloud storage and disable local saves. The port now enables the existing local path, preserving native serialization, codecs, loader behavior and settings. The checked SaveIndex write gate no longer requires a previous file or cloud-ready flag for first offline creation.

HOME focus notifications run before system suspension by selecting the port-specific NoSuspend mode. The existing unfocused loop pauses gameplay and rendering; focus loss and orderly exit publish only already queued native checkpoints, then flush/commit SD writes. No profile folders are deleted, no cloud readiness is fabricated and no arbitrary live mission state is saved.

The compiled ARM regression reproduces both old omissions and passes actual native serialization, compression, dual file encoding, local reload/application, updated-value reload, layout rejection and pending-only lifecycle gates. Existing host/native regressions also pass. Complete mission resume after HOME closure, live focus delivery and sleep behavior still require Switch testing. Preserve all `data/`, `gameloft/` and `sdcard/` folders when replacing the NRO.

## 2026-10-04 startup correction

User hardware logs established successful setup/loading/JNI followed by a repeatable null-pointer abort in `setPaths`. Move path setup after `InitViewSettings`, which creates the native OS object, and avoid duplicate callback/explicit setup. The native ARM regression reproduces the original PC/FAR and passes after the corrected order. The updated NRO still needs a Switch retest beyond this startup point.

## 2026-10-05 installer-state correction

Build `202610042014` hardware logs confirm successful path setup and EGL/shared-context creation, followed by the first-step abort at native `0x3fd4f4` / FAR `0`. Initialize the four verified native installer synchronization allocations before engine startup, with layout/relocation checks and allocation rollback. Production host tests and an original-ARM regression pass. Complete the observed `sGetMilliseconds()J` JNI binding with millisecond precision and register the empty Intent constructor. The new NRO needs a Switch retest beyond this engine initialization point.

## 2026-10-05 SD-capacity correction

Build `202610042331` reached the rendered loading screen, ended FastLoad and submitted the first OpenSL buffer. The user then encountered the 256 MB free-space warning at 45%. Replace the JNI libc space query and native fixed statfs capacity with actual Horizon 64-bit SD free/total bytes, preserve the guest ABI, and add MB/KB bindings plus bounded capacity diagnostics. Production capacity tests and cross-build/package checks pass. The updated NRO needs a console retest past 45%; that build did not bypass a space check.

## 2026-10-05 requested 256 MB warning removal

Build `202610050735` correctly measured 3,881.875 MiB free on the user's SD but still displayed the warning. The native preflight divides the JNI float by 1024, so change JGetFreeDiskSpace from MiB to KiB. At the user's explicit request, disable the independently verified 1.2.8d 256 MB preflight before it queries capacity or creates its modal. Guard all instruction/message preimages and modify only its ARM entry. Production host and compiled ARM regressions pass, including reproduction of the old units failure and absence of query/state changes in the bypass. The next hardware run with build 822 passed this warning and reached the main menu according to the user.

## 2026-10-05 requested Android/online notification removal

Build `202610050822` reached the main menu, but the offline-play notice could not be dismissed. Remove 93 version-checked Android update, offline/network/server, cloud-reminder/upload-offer, social and rating notification calls. Run required original acknowledgements after native step so the UI continues normally; skip Android update, decline cloud upload and select never-rate. Preserve gameplay, destructive reset, data-conflict and purchase confirmations. Host queue tests and linked ARM/private-ELF regressions pass for every patched site and real startup continuations. The new NRO still needs a console test of usable menus and gameplay.

## 2026-10-05 Play/Settings blue-screen correction

Build `202610050907` removed the offline notice but repeatedly ran its original acknowledgement, resetting the global menu state on each frame. The user's current/previous logs contain 401/207 repetitions with the same menu userdata; Play and Settings could not remain active. Bypass the verified native requestor's network preflight and follow its original pending-menu-action dispatch. FIRST_CHECK no longer queues a reset callback. The expanded ARM regression reproduces the old repeat and preserves all 18 native menu action destinations across repeated frames; host/import/package checks also pass. The updated NRO needs a Switch test of Settings, Play and gameplay.

## Gameplay stability and 60 Hz update

Hardware build 202610050928 entered gameplay with audible audio, then lost sound and crashed. Its last OBB opens failed; audout had zero reported underruns/submission failures through 288 seconds. The precise fault remains unknown without a crash-register log.

- Read-only OBB streams now share one real SD handle per archive with independent positions, reducing file/IPC resource pressure and repeated opens. Normal saves and runtime I/O remain intact.
- Verified native FPSLimit sleep is bypassed; real game timing and the original elapsed-time clamp remain. Presentation targets 60 Hz at 720p.
- NV fence emulation uses real GLES3 GPU sync with nonblocking polls; GLES2 fallback completes once per set instead of repeatedly serializing the GPU.
- Ten-second FPS/frame/heap/audio and OBB resource summaries replace verbose OBB-open logs.

Host streaming/fence tests, compiled native ARM timing and earlier regressions pass. The new build needs a long Switch session to confirm crash recovery and achieved FPS; stable 60 FPS is not claimed.
