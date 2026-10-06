# tasm2_nx technical notes

The implementation is in `source/`; the pinned android32 runtime supplies relocation/loading, Bionic, JNI objects/dispatch, path translation, setup, EGL lookup, HID helpers, audio output, crash reports, boost and the forwarder launcher. Port overrides remain in the port rather than modifying runtime source.

## Audited input and ABI

Package `com.gameloft.android.ANMP.GloftASHM`, manifest 1.2.8d / 12723, `armeabi-v7a`, ELF32 ARM. The supplied APK SHA-256 is `6211d194cb06c6cbb32c2491adef59554eef4d97763a2fbc1e4bbb52d9fcae9b`.

| Private runtime library | SHA-256 | CRC-32 |
|---|---|---|
| libgenerator.so | `3871b9c4ae5d1c491abbccc7af1744121bf964f52b4812d2f672c72ff7c062ed` | `337cc20d` |
| libtasm2.so | `d091fe95c56af681f1a06453e9868622935ecbb759c05c340fc16bf8df2ae62e` | `df28ab56` |

The hashes identify inputs; none of these files are distributed. Load generator first, then TASM2; android32 performs relocations, import resolution, kuser fixups, final protection and instruction cache flushing before constructors. `JNI_OnLoad` is required to return `0x10004`. Export addresses retain their ARM/Thumb mode bit. There are no arbitrary Thumb hooks.

The lifecycle uses verified exported `GL2JNILib` methods: `init()V`, `setPaths(String,String,String)V`, `initGL()V`, `InitViewSettings()V`, `resize(II)V`, `step()V`, `onPause()V`, `onResume()V`. The game-specific Android service classes receive their verified native initialization calls. The APK ConfigChooser invokes `initGL` then `InitViewSettings` before surface creation. `InitViewSettings` constructs the OS object used by `setPaths`; `init` only caches JNI. Path setup follows view initialization and accepts an earlier native Java callback without duplication. Calling it sooner reproduced the hardware null dereference at `0xa754f0` / address `0x10`. One warm game frame precedes native controller attachment. Unsafe process-lifetime engine destruction is avoided at shutdown; pause/exit callbacks, input release, audio teardown and file flush occur before process exit.

## JNI and local services

The explicit method table implements the requested 1.2.8d bindings, including display metrics, device information, stable SD-persisted IDs, AssetManager reads from the original APK, original APK certificate `Arrays.hashCode(byte[])`, Bundle types, local DataSharing and preferences. SUtils keyboard calls use Horizon software keyboard and the native text callback. The old Activity keyboard launcher is a no-op in the original DEX and remains so. Unknown JNI behavior is logged through the runtime rather than silently registering every possible method.

Display and Surface values advertise landscape 1280 × 720. Connectivity is consistently offline, including native connection state and Bionic networking. Billing/cloud/social/advertising return unavailable results. Costume ownership is initialized locally at the user's request, separately from billing; no purchase receipts or cloud profiles are fabricated. Native IAB operations 17 and 7 receive converted Google SKU metadata under `store_items`. Operation 17 previously received the original `items` schema, which its native consumer does not accept. Metadata delivery starts after the first native step constructs its consumers. Equipment-Shop configuration is fed to the native items parser; the separate store_items metadata parser is initialized first. The IAP price list is not substituted for the game-facing catalog. The original 9,577-byte catalog is read from the audited main OBB at byte offset 312,677,714, CRC `036b8ff1`; converted prices use integer micro-units. The original catalog and native protocol tokens are not embedded in source. A self-authored local Scarlet descriptor extends the owner-supplied catalog because its original Google set omits that suit.

## Version-checked fixes

These locations were independently checked in the supplied TASM2 native ELF. The NextOS reference helped locate behavior; Linux hooks were not copied.

- At image offset `0x435a80`, original ARM `BL` word `ebf76281` creates `UI_FIRST_CHECK`. Build 907 suppressed its display and queued native callback `0x42c2fc`. Hardware logs then showed that callback hundreds of times, selecting UI state 2 on every frame and interrupting Play/Settings. The current port makes this obsolete display call a NOP and bypasses its requestor's network gate: `0x433f50` changes from `1affffe3` (conditional BNE) to `eaffffe3` (unconditional B), retaining the same destination `0x433ee4`. That is the original pending-menu-action dispatch; neither online state nor native pending menu action is fabricated. Gate, caller predicate and dispatch preimages are checked before mutation.
- At `0x419a60`, reorder the destructor's resource-null check ahead of Clear. Preimage words `ebffff9e e5967064 e3570000 0a000030` become `e5967064 e3570000 0a000031 ebffff9b`; verified Clear at `0x4198e0` dereferences the resource pointer before testing it. Branch targets remain the original ones.

Native CRC and instruction preimage checks gate the patches. All current instruction changes occur in the staged writable image; android32 performs normal final protection and cache maintenance.

## Native installer state

The original engine startup at `0x3fd4c4` writes through worker pointers `0x13b7b34` and `0x13b7b3c`. Android's `GameInstaller.nativeStart` (`0xe6008`) allocates them from the one-entry counts at `0x13b7b30` / `0x13b7b38`; original count allocation paths write the value 1. These four BSS destinations have relocated GOT entries `0x133a8bc`, `0x133a8b4`, `0x133a8c0`, `0x133a8b8` respectively. The wrapper performs this synchronization preparation before any engine worker begins, using separate four-byte process-lifetime allocations initialized to 1.

The NextOS loader's `asm2_installer_compat_initialize` demonstrates the same four-allocation approach; every offset, relocated destination and native store used here was independently checked in the supplied 1.2.8d ELF. Wrong instruction layout, GOT relocation, short mapping or partial existing state fails closed. Existing complete state is preserved. A failed allocation publishes nothing. No engine instruction is removed for this fix.

## Graphics

GLES2 RGBA8, requested depth24/stencil8 with a depth16/stencil0 configuration fallback, 1280 × 720 and swap interval 1. Up to four shared loader contexts are created only when `EGL_KHR_surfaceless_context` is advertised; the actual count is returned to the game. GL imports are resolved through `dcr_gl_lookup`, including OES extension entry points.

NV fence generation/deletion/state is synchronized. On the observed GLES 3.2 Mesa driver, each SetFence uses `glFenceSync` and flushes its source context; TestFence polls `glClientWaitSync` without blocking, and FinishFence waits for that specific fence. Replacement/deletion frees the sync object. GLES2 or sync-allocation fallback uses one real `glFinish` at SetFence and remembers completion, avoiding additional waits for unrelated later commands. The menu cursor restores framebuffer, viewport, program, VBO, vertex attribute and raster state after drawing. Runtime diagnostics record compression formats, missing extension addresses and bounded GL errors at allocation/attachment calls, not every draw. Encoded OBB texture formats and actual upload compatibility still require hardware evidence. No unsupported-format decode path is claimed as validated.

After native `step` and a successful EGL swap, the wrapper calls `dcr_boost_first_picture()` and `dcr_boost_launch_end()`. Unfocused execution pauses and sleeps instead of busy-spinning. Slow frames are reported at most once per five seconds.

## Audio

`RT_OPENSLES=1`. The port's `opensles.c` is the MIT android32 implementation with narrow TASM2 queue/lifetime fixes; its dedicated worker still uses `rt_audout` and converts PCM to 48 kHz stereo. No second Vorbis mixer is introduced.

The worker copies queued guest PCM while holding the queue mutex; Clear/Stop/Destroy change a generation counter so stale completion cannot dequeue a freshly replaced buffer. Empty PLAYING queues receive a bounded refill callback to recover the game's mixer. Destroy waits for in-flight callbacks. Only the audited callback at `0xbb7c98` may temporarily hand off its owned mixer mutex at `*(context+4)`, with Bionic magic, readable-range and thread-owner checks, before waiting. Callback lock depth is restored afterward. HOME/focus changes pause the audio route. The user confirms audible gameplay on build 928 before a later loss of sound and crash. Its audout statistics show zero underruns/failed submits/dropped buffers through 288 seconds. The accompanying terminal OBB-open failures suggest resource pressure; no crash PC or proof of causation is available. Sustained audio, volume, starvation recovery and suspend/resume still require hardware tests.

Production OpenSL host tests also pass for copied guest PCM after retirement, Clear-generation protection, dry-queue refill and waiting for an in-flight callback during Destroy. These tests use a deterministic audout backend; they do not establish audible Switch output or the native mixer lock handoff.

## SD capacity reporting

`source/tasm2_space.c` obtains the existing mounted SD filesystem through libnx32 and calls Horizon's 64-bit free/total byte APIs. Both JNI `JGetFreeDiskSpace()F` and `SUtils.getFreeSpace(String)F` receive KiB. The SUtils DEX specifies bytes / 1024; the native JGetFreeDiskSpace path at `0x3c6744` multiplies its float result by `1/1024` to obtain MiB. The native statfs overlay delegates structure initialization to android32, then replaces its fixed capacities with actual 64-bit block counts. The original native free-space consumer at `0x10b8504` reads guest `f_bsize` at byte 4 and 64-bit `f_bavail` at byte 24, multiplies and shifts by 20; these offsets match the reused Bionic statfs layout.

The first successful query logs SD bytes and MiB. Query failures, missing mounts or invalid counters return failure/zero as appropriate and log a bounded diagnostic. Capacity APIs continue to report real values; the separately requested startup-warning bypass is described below. Production tests use mocked Horizon queries to verify high capacities, the 256 MiB boundary and errors. A separate Unicorn test executes the compiled ARM overlay and original native consumer using the produced guest structure; 64-bit counters, MiB arithmetic and error cases pass. Build 735 confirmed the actual query on the user's console, and build 822 passed the capacity-warning point according to the user.

## Input and persistence

Native controller events have ABI `(ID)V`: ID1/2 triggers; 3/4 left axes; 5/6 camera axes; 7–10 directions; 11/12 shoulders; 13–16 Y/A/X/B; 17/18 Start/Select; 19 BACK; 20/21 stick clicks. Values are doubles using the actual AAPCS softfp convention. Deadzone is rescaled after 0.18; Switch Y axes are inverted.

`touchEvent(IIII)V` uses **1 press, 0 release, 2 move**, verified in original `onTouchEvent` and `sendTouchEvent`, not Android MotionEvent numeric assumptions. The last release sends `(0,-2000,-2000,0)`. Nine physical fingers plus cursor pointer9 fit the game's ten-pointer slots. Pause/disconnect release axes, buttons, fingers and BACK. Action-phase JNI controls the cursor; gameplay uses native HID rather than virtual movement touches.

The runtime maps package internal paths to `data/`, Android Gameloft shared paths to `gameloft/`, and discoveries to intact OBBs. Preferences have independent dynamic stores, escaped UTF-8 XML, synced temporary writes, backup recovery and failed-publish rollback in memory and on disk. Ordinary file I/O overlays delegate to Bionic fopen/fclose, log writable paths, fsync native saves and commit SD storage on pause/exit. Read-only discovered OBB files now use the shared-handle stream bridge described below; close still runs through android32 b_fclose/newlib. Hardware checkpoint persistence is not established by host preference tests.

## Performance and verification limits

The verified native timer at `0x3b1bac` stores real elapsed milliseconds and clamps them to 0–100. When FPSLimit at object field `0x328` is positive and frame pacing is requested, the original code sleeps until that minimum interval is met (e.g. 33 ms). `source/tasm2_fps.c` checks both gate and elapsed-path preimages, then changes only `0x3b1be4` from conditional BLE `da00001f` to unconditional B `ea00001f`, retaining destination `0x3b1c68`. Real timestamps, elapsed calculation and the existing clamp remain intact; no physics/animation constants or global sleep services change. EGL interval 1 provides the 60 Hz presentation target. The compiled ARM regression reproduces the original 33 ms wait, then checks 410 timing cases after the patch and rejects an altered binary. Actual sustained FPS requires hardware measurement.

Every ten focused seconds, `[perf]` records achieved presentation FPS, native-step time (including services/input), EGL presentation time, maximum frame duration, frames over 33 ms, mallinfo heap use and OpenSL progress. HOME/unfocused intervals reset measurements. OBB summaries replace per-open log writes. FastLoad still ends after first presentation; no gameplay overclock or permanent FastLoad is introduced. [VALIDATION.md](VALIDATION.md) distinguishes passed checks from hardware acceptance.

## Shared OBB streams

The build 928 log contains 9,076 traced OBB opens with 624 distinct FILE addresses (including NULL), many addresses correctly reused, followed by two failed opens. This is consistent with late file/resource exhaustion but does not prove a native stream leak, exact errno or crash address. `source/tasm2_obb.c` follows android32's existing APK `funopen` pattern for the three discovered immutable OBBs. Each logical FILE owns its seek position and normal newlib EOF/error/buffering state; all streams for an OBB share one Horizon FsFile with explicit-offset reads and a mutex-protected bounded cache of 64 KiB blocks, described below. Stream close releases its cookie and stdio buffer; the physical handle stays available for other streams. Read-only buffering is 16 KiB per stream. Ordinary files, saves, APK caching and Android path translation retain their runtime behavior. The game imports no fileno API requiring a physical descriptor for these logical streams.

Open/read errors remain failures with bounded diagnostics. Summaries record live/peak streams, opens/closes, SD reads/bytes and errors to distinguish native retained streams from heap pressure. Physical handles close at shutdown only when no logical stream remains; native process-lifetime objects retain access until process exit. Production host tests use actual newlib FILE/funopen behavior with mocked offset-based SD reads: 1,024 concurrent independent streams on two physical handles, 10,000 close/reopen cycles, concurrent threads, offsets near the 1.2 GiB OBB end, EOF, invalid seek and read-error recovery pass. The live console failure still requires a long-session retest.

## Requested 256 MB startup-warning bypass

`source/tasm2_warning.c` checks the original ARM entry at `0x3f2030`, query call at `0x3f205c`, compare/branch at `0x3f20a8`, 256 formatting value, KiB-to-MiB literal and `UI_MemoryNotEnough` key before changing one instruction. It replaces the void preflight entry with `bx lr` during the loader's writable phase; android32 then seals and flushes the image. Both verified callers (`0x3f47ac`, `0x3fe8a0`) ignore its return value. No capacity query, warning creation or warning-state update runs inside this routine afterward. File writes retain their real filesystem errors.

The registered JGetFreeDiskSpace handler is also corrected to KiB for other native consumers. The original converter (`0x3c6744`) turns 3881.875 MiB supplied in the old units into only 3 MiB; a production-handler/native regression reproduces that warning and verifies the corrected conversion. The bypass test executes the linked production patch against the hash-checked original library and verifies unchanged stack/callee-saved registers, zero query calls and untouched object state, even with zero reported capacity.

## Requested offline notification removal

`source/tasm2_popup_sites.h` records 93 explicitly reviewed call sites, four instruction preimages at each site and the original localization key. The shared ConfirmBox builders are preserved. Scope includes no-internet, server/network errors and timeouts, offline-play information, Android update/details, cloud-data reminders/upload offers, social/invite/share notifications, empty online leaderboards, online profile download notices and game-rating requests. Gameplay retry/abandon, currency checks, hard reset, purchase results and cloud-profile data conflicts keep their original handling; no purchase success or cloud overwrite is synthesized.

`source/tasm2_popups.c` first checks every site/key, original builder ABI and the entire 80-byte stub region. The latter is the now unreachable body at `0x3f2034` of the capacity preflight whose entry must already be `bx lr`. The original ELF regression scans executable direct branches and image pointer values to verify that no external caller enters this body. Every original BL is redirected to a same-image ARM stub. Its original LR and SP survive; it reads callback/userdata from the verified original arguments and tail-calls the port's queue through `ldr pc`, which supports ARM/Thumb interworking. Update-details uses its separate r3 / stack+0 callback ABI. Two simple notices have no callback and their display calls become ARM NOPs. FIRST_CHECK also becomes a NOP because its requestor is bypassed; calling its old acknowledgement would reset the menu again. No separate executable allocator or runtime loader change is introduced.

The mutex-protected queue deduplicates a pending callback/userdata/choice triple. It drains after native step, before cursor/presentation, outside its lock. New notifications queued by a continuation wait until the next frame. Choices follow the reviewed native meaning: normal one-button acknowledgement, Android-update skip (1), cloud-upload decline (1) and never-rate (2). This preserves the normal UI progression and avoids changing scenes while the old caller still owns temporary strings. Production host tests cover deferred execution, deduplication, preserved arguments, concurrent producers, chained next-frame transitions and 256 distinct queued continuations.

`tests/test_popups_native.py` runs the linked patcher, all 93 patched calls, ARM stubs and production queue against the hash-checked original ELF. It checks rejection without partial publication for altered sites/keys/builders/body and short images, verifies that only intended bytes change, and preserves SP/callee-saved registers. The regression reproduces the old repeated first-check state-2 reset, then executes the patched requestor and original native dispatch for all 18 pending action values, idle and invalid actions over repeated frames with both values of the original server-ready byte. Actions retain their original destinations; no first-check callback is queued and no pending warning byte is set. The remaining actual callbacks confirm cloud reminder 12 -> 13, update-details 10 -> 11, Android update skip and cloud upload decline. Mutex operations and UI-manager virtual services are mocked; full Switch menu interaction/gameplay remains unverified for this new build.

## Loading, fractional slow motion and equipment Shop

Build 1037 logs measure a 38,259.90 ms first native update and approximately 1.6 GiB of SD reads before its first picture. A GLES2 loading picture precedes that blocking update, with framebuffer/scissor/color-clear/write-mask state restored. The retired text console is not reopened after EGL handoff. Direct `eglSwapBuffers` presents this port picture; only the first successful native game presentation ends android32 FastLoad.

The existing one-handle OBB bridge now retains at most 256 × 64 KiB blocks per archive (16 MiB, including its embedded fallback). LRU growth follows android32's APK-cache policy: retain 256 MiB spare heap, then recycle. Three archives cap the aggregate cache at 48 MiB. Actual EOF/seek/error behavior and independent FILE positions remain. The synthetic 32-position trace needs at most 33 SD requests for 960 alternating reads; build 1242 measures startup FastLoad at 12.868 seconds; the expanded 192-block trace needs 192 physical reads for 576 logical reads. The latest capacity change needs a hardware comparison.

At `0x3e9154`, the update multiplies real elapsed milliseconds by time scale; `0x3e9158` truncates the product before its 0–100 ms clamp. At 60 Hz, 16/17 ms × 0.05 truncates to zero every frame. A checked branch at `0x3e9158` invokes a fractional accumulator via `0x3f2084..0x3f20ac`, following the popup stubs in the disabled warning body. Multiplication, downstream clamp and game update remain native. Integer registers/LR and stack alignment survive; true zero-scale pauses remain zero. Private-ELF tests reject outside branches/pointers into the cave.

The equipment Shop's phase-0 gate at `0x4abfac` requires config and actual OfflineStore readiness. Checked `mov r3,#1` at `0x4a9e04` becomes `mov r3,#0`, selecting the existing local CRM initialization at `0xc59df8`. Build 1242 still waited for catalog completion, so the new game-thread poll also initializes the native ShopManager from verified OBB data. Once the manager's descriptor map/request and IAP metadata exist, it calls native COW string construction (`0x1128de8`) and the actual items parser (`0x3986d4`). Nonempty category counts from `0x37d488` are required before advancing to the original state-5 continuation. Empty/rejected catalogs retain the original nonready state. All used APIs/global references and the native completion sequence have checked preimages. No network response or billing-success event is synthesized.

`ItemData.xml`, `IapProfileMap.json` and `ShopItemPriority.json` independently identify seven additional suits: Scarlet, Superior, Iron, Symbiote, Ultimate Comics, Future Foundation (`Feature_Spider_suit` is the original identifier), and Ends of the Earth. The original Google catalog has six suit descriptors; a self-authored `Suit_Sca`/`Scarlet_Spider_suit_offline` row adds Scarlet to both catalog schemas. The parser rejects zero-priced rows, so this local row uses an internal positive placeholder with display text `Owned`; ownership is supplied locally and purchase services remain unavailable. The default costume is retained by the game's existing UI.

On the native game thread, all seven profile names are validated against the inventory map before any write. Native hash (`0x220bf8`), map lookup (`0x52318c`) and encrypted integer accessors (`0x1b888c`/`0x1b8938`) are used. Only missing/nonpositive costume owned/available quantities at +4/+0x20 become 1; existing positive quantities, levels, currency, noncostume items and progress remain. Native checkpoints serialize profile quantities normally; the availability grant is also reapplied on startup. The Suit filter at `0x3756b8` uses descriptor type **2** and availability byte +0x70; type 3 is the currency view, while JSON category index 1 is Suits. One active descriptor per verified suit is retained across price/profile refreshes. The original UI's owned-quantity accessor (`0x52bc24`) reads the granted values, keeping the normal equip path available. Rendering, actual equip/save/relaunch and pause-menu/main-menu entry still need a Switch test.

## Gameplay SD write pacing

Native analytics under the exact `/data/files/gv3/` path still uses ordinary Bionic fclose. Explicit per-close fsync is reserved for the game's other writable files; filenames such as `gv3.sav` and the `gv30` directory remain durable. All open writers, including telemetry, are flushed/fsynced on lifecycle flush and SD is committed. File-open diagnostics stop after 16 durable openings; errors remain visible. No checkpoint or preferences file is deleted or skipped.


## Native offline UserData persistence

The settings-only hardware report matches the actual 1.2.8d UserData design: the base constructor at `0x51e8a0` sets cloud byte +0x18 to 1 and local byte +0x19 to 0; settings constructors explicitly opt into local saves. Progress uses the native `ud_%s.sav` writer at `0x547350` and loader at `0x5475f0`, under the internal files path returned by `0x3c6604`. Retained legacy `spiderman2.sav` functions are not used as a substitute.

`source/tasm2_checkpoint.c` checks constructor/writer/loader/publisher/update instruction preimages before mutation. Three instructions at `0x51e8ec` zero flags +0x1c..+0x1e plus padding +0x1f and set local byte +0x19 to 1. JSON begins at +0x20; all constructor arguments and other flag values are preserved. Derived settings keep their original values. A NOP at `0x547384` routes SaveIndex type 16 through the original writer even on the first offline run; profile existing-file +0xb50 and cloud-ready +0xb52 are not fabricated.

The original native publisher (`0x547f20`) uses JSON metadata, a native compressed/encoded COW string and a length-XOR header with two encoded payload copies. No custom save format or synthetic profile content is inserted. Local initialization (`0x548bf4`) reloads the original file and applies its native decoder/deserializer. On the main game thread, focus loss/exit can publish only already dirty UserData cache blocks from profile +0x8d8, using object pointers at +0x830 and the same readiness gates as `0x54882c`. Cached raw-buffer +0 can legitimately be zero; serialized COW backing is at +0x10. No live mission snapshot or reset/reload routine is forced.

After runtime lifecycle registration, the port requests `AppletFocusHandlingMode_NoSuspend` before the frame loop, logging the Result. This allows HOME focus loss to be processed before Close Software, while the existing unfocused branch skips gameplay and EGL presentation. Pending native blocks are published before JNI pause, then ordinary durable I/O and `fsdevCommitDevice` finish the writes. Runtime source remains unchanged. Hardware focus delivery/mission resume/sleep are acceptance items, separately from the passed native file round trips.


## Medium-detail performance paths (audited 1.2.8d)

`source/tasm2_performance.c` guards all sites/keys before writing four ARM words.
At `0x3fda08`, the `MemoryEnableMMap` boolean read becomes `mov r0,#0`, storing
false at native flag `0x134f4e0` through the original PIC GOT/store. Allocator
`0x1a1cb4` reads that flag at `0x1a1d20`: requests with bit 0x400 and fallback
requests enter the existing `malloc`/zero-fill branch at `0x1a1d58`. Ordinary
requests retain their original path. Deallocation `0x1a5a48` reads the same flag
and calls original free, rather than the MMap arena finder. The old native mmap
route can create 64 MiB `MMapAlloc.dump%d` files at `0x4d8c84`; android32's
`bionic_mem.c` then allocates the whole map in RAM and reads the file. Removing
that route eliminates redundant temporary SD traffic without replacing any
allocator or save codec. Neither save files nor old dump files are deleted.

At `0x3fbcac..0x3fbcb4`, MOVW/MOVT r0 and NOP supply softfp float return bits for
the single named `RTA_ScaleRatio` read. Original VFP comparison/multiplication/
integer conversion `0x3fbccc..0x3fbd1c` and render setup continue. Native string
release remains unchanged. Default 0.80 yields 1024x576 from 1280x720. Original
owner `RenderTarget_android_mid.xml` has percent-based scene/depth dimensions;
render assembly setup uses the scaled base to size targets. Other config reads,
medium detail selection, view/input coordinates, time constants and presentation
are untouched. All guard addresses and native behavior were checked against
1.2.8d, not copied from Total Mayhem. The allowed configuration range is .75–1.0;
visual effects/UI sharpness and achieved FPS require console validation.

OBB cache hit lookup uses fixed hash chains (512 buckets, slot index+1 links)
under the existing archive mutex. Recycled or failed entries are unindexed before
SD reads, and success publishes the new entry. LRU stamps/budget/read semantics
remain; misses still scan the bounded slots to select LRU. There is no unbounded
allocation or speculative read-ahead. Shutdown clears bucket/slot state before
reopen. Probe telemetry and synthetic byte-checked traces demonstrate hit lookup
cost reduction, not hardware FPS.

`RT_BOOST_WATCH_THREAD=1` reuses android32's 10 ms loading watcher.
`RT_GL_SWAP_ENDS_FRAME=0` lets the port bracket its own native frame: begin before
services/input/step, end after fixes/cursor and before EGL presentation. HOME,
unfocused polling and shutdown call `dcr_boost_idle`; launch boost still ends on
the first game presentation. `dcr_boost_cpu_report` samples existing thread CPU
ticks about once per minute. GPU fences remain the verified GLES3 sync bridge;
no fake completion or new per-frame glFinish is added. Runtime, Mesa and libnx32
sources are unchanged. No custom overclock or persistent FastLoad is introduced.

The native performance regression also runs the real combined loader patch
preparation after applying payload PIE relocations, checking that the new
choices coexist with checkpoint, Shop, warning/popup and finisher changes.

## Saved-profile inventory startup crash (build 202610051919)

The supplied crash log has four entries spanning payloads 1703 and 1816. The
latest `tasm2_nx+0x534e10` PC is in libc `strcmp`; its caller return offset
`+0x14694` is inside `unlock_costumes`. All four faults use `0x268db39d` as the
input address. Build 1816 is archived before replacement, retaining its ELF
for symbol resolution. The crash is reproduced by the linked old wrapper
with an inventory fixture whose `+0x14` word has that exact value.

In native 1.2.8d, `0x220bf8` constructs the numeric name key, and `0x52318c`
performs unsigned tree lookup, checks exact key equality, then returns an
inline value at node `+0x14`. This node offset does not establish a string
at value `+0x14`. Native identity consumers use the numeric ID at value `+0`
and `0x205014` for its string lookup. The wrapper's additional `strcmp` check
was invalid and has been removed; the all-seven-exist guard is retained.
The existing original encrypted getters/setters and owned/available fields
are unchanged; positive quantities and upgrades are not reset. No userdata
writer, reader, profile flag, saved file or file format changes in this fix.

`tests/test_shop_catalog_native.py --expect-legacy-name-crash` verifies the
old fault. The ordinary mode uses actual ARM hash/map/cipher/catalog/UI code,
requires that unused `+0x14` words are untouched, recreates the numeric map at
new addresses and checks preservation through subsequent ticks. Map topology,
unused words and decoded inventory snapshot are synthetic. The separate
checkpoint test executes real native UserData serialization/file codecs and
reload. Neither test proves complete gameplay checkpoint application on a
physical Switch. Retest a full console reboot with the existing saved profile.
