#!/usr/bin/env python3
"""Verify the redistributable android32 payload and NRO without game files."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

TITLE = 'The Amazing Spider-Man 2'
AUTHOR = 'Bora Eskicioğlu / BESK Entertainment'
PROGRAM_ID = 0x055A3A4744DC9001


def require(ok, message):
    if not ok:
        raise ValueError(message)


def take(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            f'Out-of-bounds region {offset}+{size}')
    return data[offset:offset + size]


def u32(data, offset):
    return struct.unpack('<I', take(data, offset, 4))[0]


def u64(data, offset):
    return struct.unpack('<Q', take(data, offset, 8))[0]


def cstr(data):
    return data.split(b'\0', 1)[0].decode('utf-8')


def elf(path, elf_class, machine):
    data = path.read_bytes()
    require(data[:4] == b'\x7fELF', f'{path.name}: invalid ELF')
    require(data[4] == elf_class and data[5] == 1, f'{path.name}: wrong ELF class/endian')
    require(struct.unpack('<H', take(data, 18, 2))[0] == machine,
            f'{path.name}: wrong architecture')


def pfs(data):
    require(data[:4] == b'PFS0', 'NSP must be an ExeFS PFS0')
    count, string_size = u32(data, 4), u32(data, 8)
    require(count < 16, 'Unexpected NSP file count')
    string_base = 16 + 24 * count
    strings = take(data, string_base, string_size)
    base = string_base + string_size
    files = {}
    for i in range(count):
        entry = 16 + 24 * i
        string_offset = u32(data, entry + 16)
        require(string_offset < len(strings), 'Bad PFS string offset')
        name = cstr(strings[string_offset:])
        require(name not in files, 'Duplicate PFS entry')
        files[name] = take(data, base + u64(data, entry), u64(data, entry + 8))
    return files


def romfs(data):
    require(u64(data, 0) == 0x50, 'Unexpected RomFS header')
    dirs = take(data, u64(data, 24), u64(data, 32))
    meta = take(data, u64(data, 56), u64(data, 64))
    require(u32(dirs, 8) == 0xffffffff, 'Unexpected RomFS subdirectory')
    entry = u32(dirs, 12)
    files = {}
    seen = set()
    while entry != 0xffffffff:
        require(entry not in seen and len(seen) < 16, 'Invalid RomFS file chain')
        seen.add(entry)
        require(u32(meta, entry) == 0, 'Unexpected RomFS parent')
        name = take(meta, entry + 32, u32(meta, entry + 28)).decode('utf-8')
        require(name not in files, 'Duplicate RomFS entry')
        files[name] = take(data, u64(data, 72) + u64(meta, entry + 8), u64(meta, entry + 16))
        entry = u32(meta, entry + 4)
    return files


def verify(root):
    elf(root / 'tasm2_nx.elf', 1, 40)
    elf(root / 'launcher/tasm2_nx.elf', 2, 183)
    nsp = (root / 'tasm2_nx.nsp').read_bytes()
    files = pfs(nsp)
    require(set(files) == {'main', 'main.npdm'}, 'NSP contains unexpected content')
    require(files['main'][:4] == b'NSO0', 'Invalid payload NSO')
    npdm = files['main.npdm']
    require(npdm[:4] == b'META' and not (npdm[12] & 1), 'NPDM must request AArch32')
    aci = npdm.find(b'ACI0')
    require(aci >= 0 and u64(npdm, aci + 16) == PROGRAM_ID, 'Wrong payload program ID')
    nro_path = root / 'launcher/tasm2_nx.nro'
    nro = nro_path.read_bytes()
    require(nro[16:20] == b'NRO0', 'Invalid launcher NRO')
    aset_offset = u32(nro, 24)
    aset = nro[aset_offset:]
    require(aset[:4] == b'ASET', 'Missing NRO assets')
    icon = take(aset, u64(aset, 8), u64(aset, 16))
    nacp = take(aset, u64(aset, 24), u64(aset, 32))
    require(icon[:2] == b'\xff\xd8', 'Missing JPEG icon')
    require(icon == (root / 'launcher/icon.jpg').read_bytes(), 'NRO embeds a stale launcher icon')
    require(len(nacp) == 0x4000, 'Unexpected NACP size')
    require(cstr(nacp[:512]) == TITLE, 'Wrong launcher title')
    require(cstr(nacp[512:768]) == AUTHOR, 'Wrong launcher author')
    require(cstr(nacp[0x3060:0x3070]) == '1.0.0', 'Wrong launcher version')
    contents = romfs(take(aset, u64(aset, 40), u64(aset, 48)))
    require(set(contents) == {'tasm2_nx.nsp', 'tasm2_nx.build'},
            'RomFS must contain only the open-source wrapper payload and build stamp')
    require(contents['tasm2_nx.nsp'] == nsp, 'NRO embeds a stale payload')
    stamp = (root / 'tasm2_nx.build').read_bytes()
    require(contents['tasm2_nx.build'] == stamp, 'NRO embeds a stale build stamp')
    build = stamp.decode('ascii').strip()
    require(build.isdigit(), 'Invalid build number')
    report = {'title': TITLE, 'author': AUTHOR, 'version': '1.0.0',
              'program_id': f'{PROGRAM_ID:016X}', 'build_number': build,
              'nro_bytes': len(nro), 'nro_sha256': hashlib.sha256(nro).hexdigest(),
              'icon_bytes': len(icon), 'icon_sha256': hashlib.sha256(icon).hexdigest(),
              'nsp_bytes': len(nsp), 'nsp_sha256': hashlib.sha256(nsp).hexdigest(),
              'romfs_files': sorted(contents), 'payload_architecture': 'ELF32 ARM / AArch32',
              'launcher_architecture': 'ELF64 AArch64', 'hardware_tested': False}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.root)
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.report:
        args.report.write_text(rendered + '\n', encoding='utf-8')
    print(rendered)


if __name__ == '__main__':
    main()
