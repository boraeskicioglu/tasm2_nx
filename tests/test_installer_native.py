#!/usr/bin/env python3
"""Reproduce the second Switch abort with a privately supplied original ELF.

Execute the real installer JNI entry points and the faulting ARM preamble.
Only JNI services/malloc are mocked. No game library or data is bundled.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

BASE, HEAP, STACK, RETURN = 0x10000000, 0x30000000, 0x40000000, 0x50000000
ENV, TABLE, CLASS = HEAP + 0x100, HEAP + 0x200, HEAP + 0x600
GLOBAL_REF, METHOD_ID, CALL_VOID = HEAP + 0x1000, HEAP + 0x1010, HEAP + 0x1020
STATES = (0x13b7b30, 0x13b7b34, 0x13b7b38, 0x13b7b3c)
GOT = (0x133a8bc, 0x133a8b4, 0x133a8c0, 0x133a8b8)
INIT, START, ENGINE, FAULT, CHECKPOINT = 0xe5f08, 0xe6008, 0x3fd4c4, 0x3fd4f4, 0x3fd504


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    args = parser.parse_args()
    raw = args.library.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == 'd091fe95c56af681f1a06453e9868622935ecbb759c05c340fc16bf8df2ae62e', 'Unsupported native variant'
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    with args.library.open('rb') as stream:
        elf = ELFFile(stream)
        loads = [seg for seg in elf.iter_segments() if seg['p_type'] == 'PT_LOAD']
        end = max(seg['p_vaddr'] + seg['p_memsz'] for seg in loads)
        mu.mem_map(BASE, (end + 4095) & ~4095)
        for seg in loads:
            mu.mem_write(BASE + seg['p_vaddr'], seg.data())
        relocations = {r['r_offset']: r['r_info_type'] for sec in elf.iter_sections()
                       if hasattr(sec, 'iter_relocations') for r in sec.iter_relocations()}
    mu.mem_map(HEAP, 0x10000)
    mu.mem_map(STACK, 0x10000)
    mu.mem_map(RETURN, 4096)
    def word(address):
        return struct.unpack('<I', bytes(mu.mem_read(address, 4)))[0]
    def store(address, value):
        mu.mem_write(address, struct.pack('<I', value))
    def string(address):
        return bytes(mu.mem_read(address, 100)).split(b'\0')[0].decode('ascii')
    for got, state in zip(GOT, STATES):
        assert relocations[got] == 23 and word(BASE + got) == state
        store(BASE + got, BASE + state)  # R_ARM_RELATIVE, as the runtime performs.
    store(ENV, TABLE)
    for offset, callback in ((0x54, GLOBAL_REF), (0x1c4, METHOD_ID), (0x234, CALL_VOID)):
        store(TABLE + offset, callback)
    methods, allocations, calls, faults = {}, [], [], []
    def invalid(uc, access, address, size, value, data):
        faults.append((uc.reg_read(UC_ARM_REG_PC) - BASE, address))
        return False
    def code(uc, address, size, data):
        relative = address - BASE
        if relative == CHECKPOINT:
            uc.emu_stop()
            return
        if ENGINE <= relative < CHECKPOINT or INIT <= relative < 0xe60d8 or 0xe62c8 <= relative < 0xe62d4:
            return
        if address == GLOBAL_REF:
            uc.reg_write(UC_ARM_REG_R0, uc.reg_read(UC_ARM_REG_R1))
        elif address == METHOD_ID:
            name, signature = string(uc.reg_read(UC_ARM_REG_R2)), string(uc.reg_read(UC_ARM_REG_R3))
            methods[name] = (len(methods) + 1, signature)
            uc.reg_write(UC_ARM_REG_R0, methods[name][0])
        elif address == CALL_VOID:
            assert uc.reg_read(UC_ARM_REG_R2) == methods['startGame'][0]
            calls.append('startGame')
        elif relative == 0xa5a20:
            size = uc.reg_read(UC_ARM_REG_R0)
            assert size == 4
            pointer = HEAP + 0x8000 + len(allocations) * 16
            allocations.append((pointer, size))
            uc.reg_write(UC_ARM_REG_R0, pointer)
        else:
            raise AssertionError(f'Unexpected external call {address:#x}')
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    mu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    mu.hook_add(UC_HOOK_CODE, code)
    def run(entry):
        mu.reg_write(UC_ARM_REG_SP, STACK + 0xf000)
        mu.reg_write(UC_ARM_REG_LR, RETURN)
        for reg, value in ((UC_ARM_REG_R0, ENV), (UC_ARM_REG_R1, CLASS), (UC_ARM_REG_R2, 0), (UC_ARM_REG_R3, 0)):
            mu.reg_write(reg, value)
        mu.emu_start(BASE + entry, RETURN, count=10000)
    assert word(BASE + FAULT) == 0xe5823000
    try:
        run(ENGINE)
    except UcError:
        assert faults == [(FAULT, 0)], faults
    else:
        raise AssertionError('Missing installer state must reproduce the hardware abort')
    faults.clear()
    run(INIT)
    assert methods == {'startGame': (1, '()V'), 'getSDFolder': (2, '()Ljava/lang/String;')}
    # Native nativeAllow's allocation paths supply exactly one entry per count.
    for offset, pointer in ((STATES[0], HEAP + 0x7000), (STATES[2], HEAP + 0x7010)):
        store(BASE + offset, pointer)
        store(pointer, 1)
    run(START)
    assert len(allocations) == 2 and calls == ['startGame']
    assert [word(word(BASE + state)) for state in STATES] == [1, 1, 1, 1]
    # Verify the real engine can change both workers without corrupting counts.
    store(word(BASE + STATES[1]), 0)
    store(word(BASE + STATES[3]), 0)
    run(ENGINE)
    assert mu.reg_read(UC_ARM_REG_PC) == BASE + CHECKPOINT and not faults
    assert [word(word(BASE + state)) for state in STATES] == [1, 1, 1, 1]
    print('installer native: missing state reproduces PC +0x3fd4f4 / FAR 0; original nativeStart establishes four one-entry values; engine writes both workers successfully')
    print('JNI/malloc are mocked and execution stops at the next engine call; Switch gameplay remains unverified.')


if __name__ == '__main__':
    main()
