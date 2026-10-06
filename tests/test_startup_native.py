#!/usr/bin/env python3
"""Reproduce the supplied setPaths crash using a privately supplied original ELF.

Only the native view/path routines execute. External factories and platform calls
are mocked; this is a lifecycle contract regression, not game/hardware emulation.
Requires unicorn and pyelftools. No game code or data is bundled with this test.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

BASE = 0x10000000
HEAP = 0x30000000
STACK = 0x40000000
RETURN = 0x50000000
OBJECT = HEAP + 0x1000
PATH_OBJECT = HEAP + 0x2000
VTABLE = HEAP + 0x3000
VIRTUAL_INIT = HEAP + 0x4000
VIEW = 0xa843ec
PATHS = 0xa754c8
FACTORY = 0x3b13dc
FAULT = 0xa754f0


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
    mu.mem_map(HEAP, 0x10000)
    mu.mem_map(STACK, 0x10000)
    mu.mem_map(RETURN, 4096)
    def word(address):
        return struct.unpack('<I', bytes(mu.mem_read(address, 4)))[0]
    def store(address, value):
        mu.mem_write(address, struct.pack('<I', value))
    global_block = BASE + 0xa754e8 + 8 + word(BASE + 0xa755a8)
    view_block = BASE + 0xa843f8 + 8 + word(BASE + 0xa84524)
    assert global_block == view_block
    native_os_slot = global_block + 0xc0
    assert word(native_os_slot) == 0
    assert word(BASE + FAULT) == 0xe5930010
    store(OBJECT, VTABLE)
    store(OBJECT + 0x10, PATH_OBJECT)
    store(VTABLE + 0x14, VIRTUAL_INIT)
    mu.mem_write(VIRTUAL_INIT, bytes.fromhex('1eff2fe1'))
    faults, setters, factories = [], [], []
    def invalid(uc, access, address, size, value, data):
        faults.append((uc.reg_read(UC_ARM_REG_PC) - BASE, address))
        return False
    def code(uc, address, size, data):
        relative = address - BASE
        if VIEW <= relative < 0xa84524 or PATHS <= relative < 0xa755a8:
            return
        if relative == FACTORY:
            factories.append(word(native_os_slot))
            uc.reg_write(UC_ARM_REG_R0, OBJECT)
        else:
            if relative in (0xa433c4, 0xa433ec, 0xa43414):
                assert uc.reg_read(UC_ARM_REG_R0) == PATH_OBJECT
                setters.append(relative)
            uc.reg_write(UC_ARM_REG_R0, 1)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    mu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    mu.hook_add(UC_HOOK_CODE, code)
    def run(entry):
        mu.reg_write(UC_ARM_REG_SP, STACK + 0xf000)
        mu.reg_write(UC_ARM_REG_LR, RETURN)
        for register, value in [(UC_ARM_REG_R0, HEAP + 0x100), (UC_ARM_REG_R1, HEAP + 0x200), (UC_ARM_REG_R2, HEAP + 0x300)]:
            mu.reg_write(register, value)
        mu.emu_start(BASE + entry, RETURN, count=10000)
        assert mu.reg_read(UC_ARM_REG_PC) == RETURN, 'Routine did not return'
    try:
        run(PATHS)
    except UcError:
        assert faults == [(FAULT, 0x10)], faults
    else:
        raise AssertionError('Original early-setPaths sequence must reproduce the hardware abort')
    faults.clear()
    run(VIEW)
    assert factories == [0] and word(native_os_slot) == OBJECT
    run(PATHS)
    assert not faults and setters == [0xa433c4, 0xa433ec, 0xa43414]
    run(VIEW)
    assert len(factories) == 1, 'View initialization must preserve an existing OS object'
    print('startup: early setPaths reproduces PC +0xa754f0 / FAR 0x10; InitViewSettings first permits all three path setters')
    print('Native external calls are mocked; this does not validate Switch boot or gameplay.')


if __name__ == '__main__':
    main()
