#!/usr/bin/env python3
"""Check the compiled statfs bridge and private 1.2.8d native space consumer.

Horizon byte queries and guest errno storage are mocked. The production ARM
bridge, android32 b_statfs initializer and native block-to-MiB arithmetic run.
No game library is bundled. Requires Unicorn and pyelftools.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_SP, UC_ARM_REG_D16, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
)

WRAPPER, GAME, HEAP, STACK, RETURN = (
    0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000,
)
CONSUMER, STOP = 0x10b8504, 0x10b8520
LIBRARY_SHA256 = 'd091fe95c56af681f1a06453e9868622935ecbb759c05c340fc16bf8df2ae62e'


def load(mu, path, base):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf['e_machine'] == 'EM_ARM'
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        assert min(s['p_vaddr'] for s in segments) == 0
        end = max(s['p_vaddr'] + s['p_memsz'] for s in segments)
        mu.mem_map(base, (end + 4095) & ~4095)
        for segment in segments:
            mu.mem_write(base + segment['p_vaddr'], segment.data())
        symbols = elf.get_section_by_name('.symtab')
        return {s.name: base + s['st_value'] for s in symbols.iter_symbols()} if symbols else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path, help='Current linked tasm2_nx.elf')
    parser.add_argument('library', type=Path, help='Privately extracted original 1.2.8d ELF')
    args = parser.parse_args()
    assert hashlib.sha256(args.library.read_bytes()).hexdigest() == LIBRARY_SHA256, 'Unsupported native variant'
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    symbols = load(mu, args.payload, WRAPPER)
    load(mu, args.library, GAME)
    for base, size in ((HEAP, 0x10000), (STACK, 0x10000), (RETURN, 4096)):
        mu.mem_map(base, size)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    path, output, stack_top = HEAP, HEAP + 0x100, STACK + 0xf000
    mu.mem_write(path, b'/sdcard/Android/data/com.gameloft.android.ANMP.GloftASHM\0')
    state = {'available': 0, 'total': 512 << 30, 'failed': False, 'errno': 0, 'queries': 0}

    def code(uc, address, size, data):
        if address == symbols['tasm2_sd_space']:
            state['queries'] += 1
            if not state['failed']:
                uc.mem_write(uc.reg_read(UC_ARM_REG_R0), struct.pack('<Q', state['available']))
                uc.mem_write(uc.reg_read(UC_ARM_REG_R1), struct.pack('<Q', state['total']))
            uc.reg_write(UC_ARM_REG_R0, 0xffffffff if state['failed'] else 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == symbols['b_set_errno']:
            state['errno'] = uc.reg_read(UC_ARM_REG_R0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    mu.hook_add(UC_HOOK_CODE, code)

    def bridge(path_arg=path, output_arg=output):
        mu.mem_write(output, b'\xcc' * 88)
        state['errno'] = 0
        mu.reg_write(UC_ARM_REG_SP, stack_top)
        mu.reg_write(UC_ARM_REG_LR, RETURN)
        mu.reg_write(UC_ARM_REG_R0, path_arg)
        mu.reg_write(UC_ARM_REG_R1, output_arg)
        mu.emu_start(symbols['sd_statfs'], RETURN, count=10000)
        assert mu.reg_read(UC_ARM_REG_PC) == RETURN, 'Bridge did not return'
        return mu.reg_read(UC_ARM_REG_R0), bytes(mu.mem_read(output, 88))

    capacities = [0, 255 << 20, 256 << 20, 257 << 20,
                  8 << 30, (20 << 30) + (128 << 20), (300 << 30) + 12345]
    for available in capacities:
        state['available'] = available
        result, guest = bridge()
        assert result == 0 and state['errno'] == 0
        block_size = struct.unpack_from('<I', guest, 4)[0]
        blocks, free, avail = struct.unpack_from('<QQQ', guest, 8)
        assert block_size == 4096
        assert blocks == state['total'] // block_size
        assert free == avail == available // block_size
        # The original success branch reads bsize at +4 and bavail at +24,
        # multiplies the full 64-bit count, then shifts bytes by 20 for MiB.
        mu.mem_write(stack_top + 0x38, guest)
        mu.reg_write(UC_ARM_REG_SP, stack_top)
        mu.emu_start(GAME + CONSUMER, GAME + STOP, count=20)
        assert mu.reg_read(UC_ARM_REG_PC) == GAME + STOP
        assert mu.reg_read(UC_ARM_REG_D16) == (available // block_size * block_size) >> 20

    state['failed'] = True
    result, guest = bridge()
    assert result == 0xffffffff and state['errno'] == 5
    assert guest == b'\xcc' * 88, 'Failure published a fake capacity'
    state['failed'] = False
    queries = state['queries']
    for path_arg, output_arg in ((0, output), (path, 0)):
        result, guest = bridge(path_arg, output_arg)
        assert result == 0xffffffff and state['errno'] == 14
        assert guest == b'\xcc' * 88 and state['queries'] == queries
    print('space native: compiled bridge + android32 ABI + original 64-bit MiB consumer pass zero, 255/256/257 MiB, >4 GiB and unaligned capacities; errors preserve output')
    print('Horizon queries/errno are mocked; actual SD results and loading past 45% require a Switch retest.')


if __name__ == '__main__':
    main()
