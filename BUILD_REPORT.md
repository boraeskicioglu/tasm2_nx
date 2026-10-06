# Build report — tasm2_nx v1.0.0

Public package prepared on **2026-10-06**. Its NRO contains the supplied
Spider-Man icon as a 256 × 256 baseline RGB JPEG. The 32-bit gameplay ELF,
ExeFS NSP and build stamp are byte-for-byte identical to the user-tested
saved-profile fix, `202610051919`. No gameplay source changes were made.

The user reports normal gameplay with no new observed problems and playable
medium-detail performance. Constant 60 FPS in every scene is not claimed.
The new icon was checked locally and inside the NRO. Its physical HOME-menu
display has not been independently tested.

| Property | Value |
| --- | --- |
| Title | The Amazing Spider-Man 2 |
| Author | Bora Eskicioğlu / BESK Entertainment |
| Version | 1.0.0 |
| Gameplay build | `202610051919` |
| Template program ID | `055A3A4744DC9001` |
| NRO | `launcher/tasm2_nx.nro` |
| NRO size | 4,150,070 bytes |
| NRO SHA-256 | `7764367926a677677ed8bf5c588679c35971c6524da165c151add9c72234bb24` |
| ExeFS NSP size | 3,829,306 bytes |
| ExeFS NSP SHA-256 | `6facfce0f8c55c2308479907fe10233c4a5d54c6b57b31690509c5e3f1fcabe9` |
| Embedded icon size | 57,894 bytes |
| Icon SHA-256 | `300d88246488766842977ac7ee74a8f3e575e2cf93c9b1c74f99512e0a8bb901` |
| Payload / launcher | ELF32 ARM / ELF64 AArch64 |
| RomFS files | `tasm2_nx.nsp`, `tasm2_nx.build` |

## Validation

Host and linked ARM save/codecs/reload, Shop/inventory, finisher/frame timing,
audio, input, OBB, popup, setup and performance regressions passed for this
unchanged gameplay payload. Publication changes artwork, documentation and
artifact checks. Gameplay regressions were not repeated without a code change.

The final artifact check validates architectures, metadata, AArch32 NPDM,
exact JPEG bytes and exact current NSP/build stamp inside the NRO. ZIP integrity,
source contents and hashes are checked before staging. APKs, OBBs, original
game libraries, saves, logs, private analysis and SDK archives are excluded.
See [VALIDATION.md](VALIDATION.md) for hardware reports and native test scope.

The artifact manifest's `hardware_tested` flag is false for the newly packaged
launcher: artifact inspection does not itself validate console behavior.
User-reported acceptance applies to its unchanged gameplay payload.

## Dependencies

- android32: `50b352cfcf5979026789c25c85d058f55acb2b62`; upstream files unchanged.
- libnx32: `5b009c39eac5268f4df189bba0a18bb073f26603`.
- mesa32 reference: `b02c9c5f9cf0b08c6e49d4c0ee52a2d8bfd7fd42`; reused compatible
  portlibs identify as **Mesa 20.1.0-rc3**. Mesa was not rebuilt for publication.
- AArch32 container: `ghcr.io/vita2hos/devcontainer/vita2hos:latest`.
- AArch64 launcher container: `devkitpro/devkita64:latest`.

Local container image IDs used by the builds:

```text
AArch32: sha256:57cbbaedf43a44fcbf8c8d91505e04ce4bfcfb7e2349ed28f9c040c338a9abae
AArch64: sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
```

Exact reproduction needs matching SDK archives, dependency revisions and build
stamp. See [README.md](README.md#building) for portable build instructions.

## Recheck and package

```sh
python3 tools/verify_artifacts.py
bash tools/test_host.sh
python3 runtime/tools/gen_imports.py --port . --check
python3 tools/package_release.py --source
```

The release ZIP contains `switch/tasm2_nx/tasm2_nx.nro` and documentation/notices.
The source ZIP includes wrapper and pinned runtime sources; SDK dependencies
must be provided separately. Archive hashes are in `release/manifest.json`.
