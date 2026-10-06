# Validation record

## Passed locally

- Clean AArch32 payload and clean AArch64 launcher cross-builds, with Mesa enabled and patched libnx32 selected.
- Import audit: **108 GL imports** delegated to the runtime GL layer; **290 non-GL imports bound** (209 shims, 81 host passthrough), **0 missing**, **0 weak-null**.
- Production SharedPreferences host tests: 32-store isolation, UTF-8/XML escaping, restart reads, interrupted-publication backup recovery, failed-publication disk and memory rollback.
- Production catalog host tests: 18 original synthetic products plus the local Scarlet descriptor, escaped strings, integer micro-prices and malformed/missing/invalid metadata. Original items/store_info remain byte-field equivalent; both catalog schemas include Scarlet. Private native tests verify the owner's 9,577-byte OBB member by CRC without exporting game data.
- Production input host tests: exact native press/release/move values, final release sentinel, native IDs, trigger/axis mapping and Y inversion, deadzone, multi-touch, pause/disconnect, action-phase cursor release and BACK release.
- `tools/verify_artifacts.py`: ELF32 ARM payload, ELF64 AArch64 launcher, AArch32 NPDM and unique program ID; correct title/author/version; current embedded NSP and build stamp; RomFS allowlist containing only those two files.
- Version guards, exported lifecycle/native signatures and ARM instruction preimages checked against the original 1.2.8d ELF and DEX. Required OBB names/sizes checked against the supplied expansions.

These checks validate the wrapper's build and tested host behavior. They do not by themselves establish Switch gameplay; supplied hardware progress is documented below.

Production OpenSL host tests also pass for copied guest PCM after retirement, Clear-generation protection, dry-queue refill and waiting for an in-flight callback during Destroy. These tests use a deterministic audout backend; they do not establish audible Switch output or the native mixer lock handoff.

## Reported startup abort and regression

`setPaths` was called before `InitViewSettings` constructed the native OS object. The original native helper dereferenced the null OS pointer at image offset `0xa754f0` (FAR `0x10`). Boot now follows the APK configuration chooser: `initGL` → `InitViewSettings` → path setup → EGL. Path setup is idempotent if the native view initialization has already requested its Java callback.

`tests/test_startup_native.py` executes the two real ARM routines using a privately supplied, hash-checked ELF. It reproduces the original PC/FAR, verifies that view initialization stores the OS object, and reaches all three path setters afterward. External factory/platform calls are mocked; this test validates the initialization contract, not the complete native constructor or Switch gameplay. Run it with `python tests/test_startup_native.py <privately-extracted-libtasm2.so>` in an environment containing Unicorn and pyelftools; the original library is supplied locally and never packaged.

## Second startup abort and installer regression

The user's next Switch run, build `202610042014`, passed view/path setup and EGL/GLES initialization, including one shared loader context. The first native engine step aborted at image offset `0x3fd4f4`, FAR `0`, instruction `e5823000` (`str r3,[r2]`). The faulting preamble loads the worker pointer through GOT slot `0x133a8b4`, targeting BSS field `0x13b7b34`; the adjacent worker at `0x13b7b3c` has the same requirement.

The original installer creates two one-entry counts and two one-entry worker arrays. `source/tasm2_installer.c` prepares these process-lifetime values after constructors/JNI_OnLoad and before the engine starts. It checks instruction preimages, four relocated GOT destinations, mapped size and all-or-none state; allocation failure frees temporary allocations before publishing any pointer. Repeated preparation preserves existing engine state. Production host tests cover these guards and rollback.

`tests/test_installer_native.py` executes the original `GameInstaller.initNative`, `nativeStart` and faulting engine preamble with only JNI/malloc mocked. It reproduces the supplied PC/FAR with zero state, verifies the native installer produces the four one-entry values, and then reaches both worker writes successfully. Use `python tests/test_installer_native.py <privately-extracted-libtasm2.so>` with Unicorn and pyelftools. Execution stops before subsequent engine initialization; this does not establish full gameplay.

The observed missing `GL2JNIActivity.sGetMilliseconds()J` now follows its DEX implementation (`System.currentTimeMillis`) with millisecond precision; `Intent.<init>()V` is registered explicitly.

## Reported 45% free-space warning

The user supplied logs and a Switch screenshot for build `202610042331`. Native installer preparation succeeded, both OBBs were read, the game submitted a 4,096-byte 32 kHz stereo OpenSL buffer, and the first EGL swap succeeded. FastLoad ended. The screenshot shows the loading screen at 45% with the message requiring more than 256 MB free space. This establishes startup progress, not completed loading/gameplay or audible audio.

The JNI disk-space handler previously used `statvfs("sdmc:/")` and returned zero silently on failure. The native `statfs` shim reported a fixed 1 GiB available, independently of the SD. Both are now backed by `fsFsGetFreeSpace` / `fsFsGetTotalSpace` on the mounted `sdmc` filesystem, using signed 64-bit byte results. The existing android32 Bionic statfs ABI remains the guest layout; its capacity fields receive the real block counts. Query errors remain errors and are logged. That build fabricated no minimum capacity and removed no engine space check; the later requested bypass is documented separately below.

Production host tests cover values above 4 GiB, 255/256/257 MiB, real zero, MB versus KB results, unavailable SD, negative/inconsistent counters and Horizon failures. The APK's SUtils bytecode explicitly divides bytes by 1024, so that method has a separate KB handler. The next hardware log should contain `[space] SD available: ...` or a query failure; That query was subsequently confirmed on the console in build 202610050735, as documented below; progression past 45% was subsequently reported with build 822, as documented below.

`tests/test_space_native.py` runs the compiled ARM statfs overlay and android32 initializer, then feeds their guest structure into the hash-checked original 1.2.8d capacity arithmetic. Real zero, 255/256/257 MiB, capacities above 4 GiB, block rounding and failure/invalid-pointer behavior pass. Only Horizon byte queries and guest errno storage are mocked. Run `python tests/test_space_native.py tasm2_nx.elf <privately-extracted-libtasm2.so>` with Unicorn and pyelftools; this does not verify the live console query or full loading.

## Repeated warning, unit regression and requested bypass

The user tested build `202610050735` and reported the same 256 MB loading modal. Its log proves the Horizon query succeeds with **4,070,440,960 bytes / 3,881.875 MiB** available. The previous direct-capacity change therefore did not resolve this modal.

The native preflight at `0x3f2030` calls the capacity helper at `0x3c6744`. That helper consumes the JNI float and multiplies it by `0x3a800000` (`1/1024`) to produce integer MiB. The previous JGetFreeDiskSpace handler supplied MiB, causing that subsequent division to yield 3 MiB for the reported SD value. Both JGetFreeDiskSpace and the DEX SUtils method now use the production KiB handler.

The user explicitly requested removal of this loading warning and its disk check. The version-checked preflight now returns before its prologue. Production host tests reject null, short and altered layouts before mutation and verify only the entry instruction changes. `tests/test_warning_native.py` checks the actual linked JNI registration/handler and native converter, reproduces the old 3 MiB warning, and verifies the corrected 255/256/257 MiB boundary and large capacities. It then executes the production patch against the original library and verifies the bypass makes zero capacity queries and preserves object state, stack and callee-saved registers. Run `python tests/test_warning_native.py tasm2_nx.elf <privately-extracted-libtasm2.so>` with Unicorn and pyelftools. Only Horizon/platform services are mocked; the user subsequently reported passing this point with build 822.

## Main menu reached; requested notification removal

The previous supplied logs identify build `202610050822`. They contain the enabled capacity-warning bypass, successful SD query (4,050,452,480 bytes / 3,862.81 MiB), successful game presentations and ended FastLoad. The previous modal-deferral code restored the original first-check builder after its predecessor closed. The user reports reaching the main menu and repeatedly pressing OK on the offline-play notice without dismissing it. These observations establish progress past the previous loading block, but not gameplay.

Build 907 removed the obsolete notification calls at 93 independently checked 1.2.8d sites, including the stuck `UI_FIRST_CHECK`. It queued original acknowledgements after engine step, rather than restoring the notification or transitioning during its caller. Android update is declined, cloud upload is declined, and rating uses never-rate. The common dialog builders and gameplay/data-conflict/purchase confirmations remain intact.

Production callback-queue host tests pass for deferred execution, duplicate suppression, argument preservation, concurrent producers, chained next-frame acknowledgement and 256 distinct pending callbacks. `tests/test_popups_native.py` originally passed its isolated acknowledgement cases against build 907 and the hash-checked original private game library: all 93 sites execute the intended stub/NOP; altered instruction/key/body guards reject before partial publication; only expected bytes change; SP and callee-saved registers survive; callbacks run once after the call returns. The actual original startup callbacks clear the first-check pending byte and select state 2, move cloud notice 12 -> 13 and update details 10 -> 11, skip Android update and decline cloud upload without entering the data-upload branch. External mutex operations and UI-manager virtual services are mocked. Run `python tests/test_popups_native.py tasm2_nx.elf <privately-extracted-libtasm2.so>` with Unicorn and pyelftools.

## Blue screen after Play/Settings; repeated-menu-reset regression

The user tested build `202610050907`, reached the main menu, and reported a blue screen after selecting either Play or Settings. BACK displayed the original exit confirmation. The current and previous supplied logs identify that build and show the original first-check acknowledgement `0x42c2fc` repeatedly: 401 calls with the same menu userdata in the current log and 207 in the previous log, plus one earlier instance in each. Other startup acknowledgements were not repeating. Each first-check callback clears its pending flag and selects global UI state 2, recreating the menu instead of allowing the selected transition to remain active. The prior one-ack regression verified the signature but missed this repeated-frame contract.

The current port bypasses the independently verified first-check requestor before it sets its warning-pending byte: the conditional ARM branch at `0x433f50` becomes unconditional with its original destination `0x433ee4`, the native pending-menu-action dispatch. The first-check display call is now a NOP and queues no state-reset callback. All other targeted notification handling remains in place; the game's normal dialog builders and exit confirmation are preserved. Native online/cloud readiness remains unchanged.

The expanded `tests/test_popups_native.py` passes on the new linked payload. It reproduces repeated state-2 resets using the original acknowledgement, demonstrates the original offline gate entering its preflight path, and verifies that the fixed gate routes all 18 original pending menu actions, idle and invalid cases to their original branch destinations over repeated frames for both server-ready values. No pending warning byte is set and no queued acknowledgement resets the scene. All 93 call-site/stub tests, instruction/key/body/gate rejection checks, register preservation and remaining startup continuation tests also pass. The test stops at menu constructor/idle branch destinations; it does not establish rendered Play/Settings or gameplay on Switch.

## Gameplay, late audio loss/crash and 60 Hz changes

The user confirms that build `202610050928` enters gameplay and played for a substantial session before audio stopped and the game crashed. This establishes gameplay progress and audible output, while stability is incomplete. The supplied current log is 9,698 lines (1,115,341 bytes); previous log documents a successful launcher self-update. audout reports 3,000/6,000/9,000 native 32 kHz buffers at 96/192/288 seconds, with **zero underruns, failed submissions and dropped buffers**. The log contains 9,076 OBB opens, many correctly reusing FILE addresses, and ends with two NULL results opening the main OBB. Without errno, active counts or a crash-register log, the exact cause is unknown; file/resource exhaustion is an inference, not a confirmed native leak or fault PC. No measured FPS was available in that build.

The current build uses one real Horizon handle per discovered read-only OBB, independent per-FILE seek positions, normal newlib stdio, a small shared read block and bounded streaming telemetry. Actual errors remain errors. Production host tests pass 1,024 simultaneously open streams on two physical handles, 10,000 close/reopen cycles, four concurrent readers, EOF/seek boundaries and SD-read failure/recovery. GPU fence host tests pass pending/completed polling, explicit finish, reset/delete and the single-completion GLES2 fallback. No fake fence completion is used.

`tests/test_fps_native.py` executes the compiled production patch and the original native timer from the privately supplied hash-checked library. It first reproduces the 33 ms cap wait, then verifies no cap sleep and unchanged real elapsed milliseconds/clamping over 410 repeated and boundary cases. Only the timer gate branch changes; an altered native layout is rejected without publication. Other startup, installer, space/warning and 93-popup/menu ARM regressions pass on the new payload. Import coverage and artifact checks pass. Local tests cannot establish that the console crash is fixed or 60 FPS is sustained.

Next hardware run: check `[gl] NV fences: GLES3 GPU sync`, `[perf]` achieved FPS/step/present/heap/audio counts, and `[obb]` active/peak/SD errors throughout a session longer than the previously failing one. If sound stops again, preserve the log immediately and include `crash.log` if generated; audio progress and heap/stream trends will distinguish the failure paths.

## Build 1037 and loading/Shop/finisher follow-up

The user confirms smooth 60 FPS gameplay on build `202610051037`. Its logs show mostly 54–60 FPS, zero OBB errors, bounded streams and healthy audio (6,000 buffers at 192 seconds, zero underruns/failed/dropped submissions). First native update is about 38 seconds, reading approximately 1.6 GiB. The equipment/costume Shop hangs from gameplay and main menu; the first tutorial finisher remains in slow motion after the attack bar fills/disappears. The earlier 30 FPS build completed it.

The loading picture and bounded OBB LRU address visibility/repeated reads. Production FILE tests retain concurrency/seek/error checks and add eviction and low-spare-heap recycling. A 32-position synthetic trace reduces 960 alternating reads to at most 33 SD requests. No hardware startup speedup is asserted yet.

`tests/test_scaled_native.py` reproduces zero progress in the original 60 Hz conversion at scale 0.05, then executes the compiled carry and ARM cave over 3,600 slow/normal updates. Cumulative time, true pauses, negative/NaN handling, the 100 ms cap, ABI/stack and fail-closed preimages pass. `tests/test_shop_native.py` reproduces the original async branch, verifies the single-byte change selects the native local path with other arguments unchanged, executes the existing offline completion and confirms the actual Shop still requires config and OfflineStore readiness. It stops before full configuration processing/Shop construction.

IAP operation-17 metadata changes from `items` to `store_items` and waits until the first native update constructs consumers. Ordinary purchase failures remain unchanged. The loading picture, startup duration, rendered Shop and tutorial finisher need a Switch retest; longer-session stability/checkpoints/HOME/sleep remain pending.

## Build 1242 hardware follow-up and current performance changes

The latest supplied logs identify build `202610051242`. The user reports good gameplay with occasional small stutters, and a persistent equipment/costume Shop loading loop when entering from the pause menu. Startup FastLoad completes in **12,868 ms** at the runtime's 1785 MHz loading boost; the preceding build logged roughly 40 seconds. The loading picture is presented before the first native step. The session reaches 12,000 audio buffers (384 seconds) with zero underruns, failed or dropped submissions, and zero OBB errors. Actual gameplay varies from around 30 to 60 FPS in heavier scenes; isolated native frame spikes reach about 100–744 ms. Heap use is approximately 475–486 MiB of 992 MiB.

Frequent `data/files/gv3/Timer.bin` and `Token.bin` writes accompany some hitches. The old wrapper explicitly flushed and fsynced every writable stream on close, including these analytics files. The new wrapper leaves telemetry close/flush to Bionic stdio, limits file-open diagnostics, and preserves explicit fflush/fsync for actual saves. Pause/shutdown still flushes/fsyncs every tracked writer and commits SD. This is a measured target for reducing SD work, not proof that all heavy-scene frame drops disappear.

OBB cache capacity increases from 8 to 16 MiB per archive, still growing only while at least 256 MiB remains spare. A 192-block working set now needs 192 physical reads for 576 logical reads; the earlier 128-block capacity cannot retain that set. Four-thread reads, 1,024 simultaneous streams, 10,000 reopen cycles, eviction, errors and memory-pressure recycling pass. The MSYS host Mutex adapter now really initializes POSIX mutexes for Horizon's zero-initialized tags; the tests assert lock/unlock success. The production Horizon runtime is unchanged.

The production save regression verifies 1,000 telemetry closes without forced fsync or open-log writes, durable checkpoint/similar-prefix handling, lifecycle flush, fflush/fsync failure propagation and bounded diagnostics. The optimization's actual frame-time effect still needs a Switch comparison.


## Fully local Shop and requested costume availability

The previous CRM-only correction did not resolve the pause-menu Shop loop on hardware build 1242. The new poll supplies the verified owner-provided `items` catalog through the original native parser, after native descriptors/request and separate IAP metadata exist. Only accepted nonempty Suit/Item counts advance the original state-5 continuation; network availability remains false.

`tests/test_shop_catalog_native.py` loads the compiled ARM payload and hash-checked original 1.2.8d library, relocates both, and runs the production catalog assembler, real IAP metadata parser, Shop parser, category counters, native hash/map/cipher code and Suit filter. Platform I/O, allocation, locks and libc are mocked. The native metadata map has 19 products; category counts are 6 currency / 7 Suits / 2 Items / 1 bundle / 3 boosts. It verifies exactly the seven distinct suit names in the type-2 view, locally owned/available quantities, existing quantity 8 preserved, a noncostume quantity left at zero, the original UI's quantity reader, no parse before metadata is ready, and later ticks without duplicate parsing/list entries. The original test fabricated a +0x14 name pointer; the restart follow-up below replaces that incorrect fixture assumption. Expanded patch tests reject alterations at every new native API guard before changing any instruction. Host catalog conversion separately checks all output metadata fields.

The linked ARM regression and host suite pass. Actual Shop rendering, selecting/equipping each costume from main/pause menus, maintaining costume upgrades and surviving checkpoint/full relaunch remain console acceptance items. The default suit and ordinary save data are retained. Purchases remain unavailable; the local costume grant does not fake billing success or grant currency.

## Hardware acceptance still pending

The user supplied real Switch logs for build `202610041953`: forwarder installation, APK/OBB discovery, extraction, ELF imports/constructors and JNI_OnLoad succeeded. audout, pthread and I/O runtime self-tests passed. Both first and later starts crashed at native `setPaths` before EGL. The next build `202610042014` verified that this path failure was fixed and reached EGL, then encountered the installer-state failure documented above. Build `202610042331` subsequently verified the installer correction and reached the loading warning documented above. Build `202610050735` verified the real SD query but reproduced the modal. Build `202610050822` passed that warning and reached the main menu according to the user; an offline notice then blocked interaction. Build `202610050907` removed the targeted notifications but repeatedly reset the menu, as documented above. Build `202610050928` subsequently entered gameplay according to the user, but encountered the late audio-loss/crash documented above. Build `202610051037` subsequently confirmed smooth 60 FPS gameplay, with the loading/Shop/finisher issues documented above. Build `202610051242` subsequently confirmed the loading picture and 12.868-second startup, with good gameplay and the remaining stutter/Shop issue documented above. The current offline Shop and SD-work changes need Switch verification. Record console/firmware/Atmosphere details and the build number before further validation:

1. First start from its sphaira HOME forwarder: package/setup errors are readable, extraction is one-time, constructors and JNI initialization complete.
2. Both retained and clean profiles pass legal/controls/loading into a usable main menu with the targeted update/cloud/offline notifications absent. Confirm A/B and touch navigation with and without the cursor, then enter gameplay.
3. A correctly rendered menu and open world at 1280 × 720; no compression upload or extension errors; FastLoad exits after first presentation.
4. Movement/camera/face buttons/triggers/shoulders, connected/disconnected controllers, nine-finger touch limits, contextual HUD/QTEs and hidden permanent mobile controls where native controller mode supports them.
5. Music, SFX, dialogue, volume changes, continuous playback and repeated pause/resume/HOME/sleep cycles without starvation or teardown deadlock.
6. New-game/checkpoint files, volume/settings and local identifiers survive a full close/relaunch; back up the data folders before interrupted-write testing.
7. Enter Shop from both main menu and gameplay pause; select/equip all seven additional costumes, return to gameplay, checkpoint and fully relaunch without losing upgrades or progress. Compare frame-time hitches against build 1242.
8. Replacing the NRO updates its own forwarder payload, preserves saves and does not modify another title.

Inspect `debug.log` and, if present, `crash.log`. Unsupported texture formats, unknown required JNI behavior or lifecycle crashes would be correctness work; actual achieved FPS and sustained stability must be measured alongside correctness.


## Settings survive; mission progress lost after HOME closure

The user clarified that settings persist, gameplay progress does not, and the prior session ended with HOME > Close Software. The latest supplied debug files still identify build `202610051242`; they show writable Language/Sound/Control/InitPos files but no progression SaveIndex files and no focus-loss checkpoint callback. This is not a console test of the new offline Shop candidate.

Native analysis identifies cloud-only defaults in UserData, its actual local writer/loader, and SaveIndex's first-write gate on an existing file or cloud-ready state. The current checked patch enables the original local path and removes only that SaveIndex condition. The port separately requests NoSuspend focus delivery and publishes already queued checkpoint blocks before pause/exit. It does not reset files or create a fake gameplay profile.

`tests/test_checkpoint_native.py tasm2_nx.elf <privately-extracted-libtasm2.so>` reproduces the old cloud-only no-file branch and the old first-offline SaveIndex omission. The linked production patch rejects null/short/changed layouts before changing bytes. Native constructors retain other flags and settings behavior. The original UserData constructor, serializers, compression, COW/base64/crypto, JSON publication, dual-copy file writer, local reader and deserializer execute with synthetic fields. A fresh native profile recovers the boolean and integer fields; an update survives another fresh-profile reload. All four existing-file/cloud-ready combinations allow SaveIndex creation without modifying the cloud flag. Pending lifecycle flush executes the real publisher once, skips clean blocks, and rejects partial/loading/resetting/not-ready objects.

Only platform I/O, allocators/libc/locks, metadata name lookup and clock formatting are mocked. Files are stored in a temporary test directory; no game data or player saves are packaged. This test verifies the actual native UserData file mechanism and production pending-block flush, not complete mission serialization or Horizon HOME notification delivery. Cross-builds, all existing production host tests, 60 FPS/scaled time, Shop/catalog/ownership, popups, space/warning, startup/installer native checks, import audit and artifact/package checks pass. Switch acceptance: complete a mission checkpoint, return to HOME, close software, relaunch and verify mission/progression plus settings; also test HOME resume, sleep and both Shop entry paths.


## Build 1703 medium-detail hardware baseline and performance candidate

The user confirms gameplay, offline Shop and saves are now working and identifies
**medium** as the graphics setting used in the latest `debug (1).log`. That log
identifies build `202610051703`: 49 non-startup ten-second samples above 25 FPS
average approximately 44.7 FPS, ranging 28.8–60.1. Native step commonly takes
18–31 ms; wrapper presentation about 0.3–0.57 ms. Waiting for GPU work can occur
inside native step, so cheap presentation alone does not establish a CPU limit.
Several native frame spikes last about 0.7–1.2 seconds. Audio/OBB summaries show
zero underruns, failed/dropped audio submissions and OBB read errors. SD-backed
`MMapAlloc.dump0` and `dump1` are opened as the native allocator creates 64 MiB
arenas. These observations identify avoidable work, without proving the cause
of every individual hitch.

The current candidate:

- Uses native `MemoryEnableMMap=false` and the existing ordinary zero-filled
  heap/free paths. android32 implements file-backed mmap as a heap allocation
  followed by a full file read, so the native disk arenas offer no VM/RAM benefit
  here. Existing save files and old temporary dump files are retained.
- Supplies `RTA_ScaleRatio` through the checked original initialization path.
  Default `render_scale=0.80` computes a 1024x576 native render-target base from
  the 1280x720 view (36% less pixel area). Medium detail settings, elapsed game
  time, Android view, touch/controller coordinates and 720p EGL surface remain.
  This scales native render targets, rather than promising pixel-sharp UI.
- Uses a fixed 512-bucket index for cache hits while retaining 256x64 KiB LRU
  capacity, 256 MiB spare-heap growth gate and one physical handle per OBB.
  A synthetic full-cache trace takes 25,600 comparisons for 25,600 hits; the
  previous linear lookup takes 3,289,600 for that same ordered working set.
  This is a host lookup-cost measurement, not a console FPS measurement.
- Enables android32's existing watcher for blocking frames over 50 ms, using
  the runtime's retail FastLoad mode. The port owns frame boundaries, restoring
  normal clocks before presentation and idling on HOME/unfocused/exit. The
  existing configuration switch can disable this boost. CPU usage/temperature
  summaries run about once per minute, not on every frame.

`tests/test_performance_native.py` passes with the linked candidate and private
hash-checked original 1.2.8d ELF. It demonstrates disk allocator selection before
the patch, then native zero-filled heap/free selection for 20 allocation cases
and the null-free guard. Native VFP dimension conversion passes .75/.80/.85/1.0
and multiple view sizes. Exactly four instructions are changed. Invalid scales,
short/null images, changed code and changed keys fail before any write. Platform
heap/string/config services are mocked; the real native branch/arithmetic runs.
The actual combined loader preparation also passes: performance, checkpoint,
Shop, warning/popup and finisher patches coexist in one original native image
with PIE globals/callbacks relocated. The host suite passes including full-cache
hits, collisions, eviction, threaded readers, failed-read recovery and close/reopen. All retained native timing,
finisher, Shop, checkpoint, popup, setup and capacity regressions pass.

The payload and launcher cross-builds, zero missing imports and artifact checks
pass; upstream android32 files remain unchanged. **Actual FPS, loading time,
visual correctness of scaled targets, heap use and scene hitches on this new
candidate are unmeasured until the next Switch run.** Compare the same medium
settings and gameplay route, including combat/finisher, costume preview/equip,
checkpoint/HOME relaunch and a longer streaming session. Logs now include hash
probe counts and CPU/boost summaries. `render_scale=1.00` is available for an
identical-resolution comparison, and `0.75` reduces pixel area by 43.75% if
needed. Preserve all generated save/profile folders on update.

## Saved-profile restart crash follow-up

The latest hardware logs show crashes after the user saved gameplay and fully
restarted the console. The initial crash entries are from build 1703; the debug
log and last crash are from build 1816. Startup imports, JNI, OpenSL and EGL
complete. Every crash resolves to the wrapper costume inventory name check,
not the new render-target or OBB optimization paths.

- The archived linked build 1816 reproduces `strcmp(0x268db39d)` with caller
  return offset `+0x14694`, matching the supplied crash log.
- The updated linked Shop regression passes real native exact hash/map lookup,
  catalog parsing, seven distinct visible suits and original native identity,
  owned and available UI readers with that word present and untouched.
- Existing quantity 8 is preserved; noncostume quantities remain zero. An
  inventory map rebuilt at new addresses retains all decoded quantities.
  1,200 later ticks do not reparse or duplicate the catalog. Partial/absent
  maps do not receive grants.
- The fixture map/unused words and decoded snapshot are synthetic. Actual
  UserData file codecs and reload are checked by the separate native checkpoint
  regression. Complete saved mission restoration is not emulated here.

Pending console acceptance: replace the NRO, boot with the current save, verify
mission/progression and settings, complete another checkpoint, close software,
and then power the console off/on and reopen. Test both Shop entry paths and
costume equip with that profile. Preserve all save/data folders; no reset or
migration is needed for this access correction.

## 2026-10-06 user acceptance and publication preparation

After testing build `202610051919`, the user reports no observed problems and
normal gameplay. Medium-detail performance is described as playable, although
it is not a locked 60 FPS. This is user-reported console acceptance; no new
numeric performance capture or exhaustive mission/sleep-cycle test was supplied.

The publication NRO replaces only its placeholder artwork with the supplied
Spider-Man icon. The 32-bit ELF, NSP and gameplay build stamp remain identical
to the tested build. Artifact checks compare the embedded JPEG with the current
launcher asset and retain the exact payload/RomFS/architecture/metadata checks.
The new artwork itself has not been checked on a physical HOME-menu forwarder.
