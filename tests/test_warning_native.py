#!/usr/bin/env python3
"""Verify the 1.2.8d warning bypass and linked JNI KiB-to-native-MiB contract.

Runs the production patch, registered JNI handler and original native routines.
Horizon byte queries and the platform float callback are mocked. No game data
is bundled. Requires Unicorn, pyelftools and the privately supplied original ELF.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R4, UC_ARM_REG_R5,
    UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
    UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_SP, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
)
from test_space_native import load, LIBRARY_SHA256, WRAPPER, GAME, HEAP, STACK, RETURN

ENTRY, CAPACITY, FLOAT_GETTER = 0x3f2030, 0x3c6744, 0xa771f8
WARNING, ENOUGH = 0x3f2158, 0x3f20b0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path)
    parser.add_argument('library', type=Path)
    args = parser.parse_args()
    assert hashlib.sha256(args.library.read_bytes()).hexdigest() == LIBRARY_SHA256
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    symbols = load(mu, args.payload, WRAPPER)
    load(mu, args.library, GAME)
    for base, size in ((HEAP, 0x10000), (STACK, 0x10000), (RETURN, 4096)):
        mu.mem_map(base, size)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    stack_top, result_buffer, object_pointer = STACK + 0xf000, HEAP + 0x100, HEAP + 0x2000
    state = {'available': 0, 'callback_kb': 0.0, 'float_queries': 0,
             'sd_queries': 0, 'stop_predicate': False}

    def word(address):
        return struct.unpack('<I', bytes(mu.mem_read(address, 4)))[0]

    def store(address, value):
        mu.mem_write(address, struct.pack('<I', value))

    def string(address):
        return bytes(mu.mem_read(address, 160)).split(b'\0')[0].decode('ascii')

    # Check the production registration table, rather than assuming a handler
    # symbol is bound to the native callback being tested.
    with args.payload.open('rb') as stream:
        elf = ELFFile(stream)
        definition = elf.get_section_by_name('.symtab').get_symbol_by_name('jni_method_defs')[0]
        table_size = definition['st_size']
    methods = {}
    for offset in range(0, table_size, 16):
        cls, name, sig, handler = struct.unpack('<IIII', bytes(mu.mem_read(symbols['jni_method_defs'] + offset, 16)))
        if not cls or not name or not sig:
            continue
        key = (string(WRAPPER + cls), string(WRAPPER + name), string(WRAPPER + sig))
        methods[key] = WRAPPER + handler
    native_binding = ('com/gameloft/glf/GL2JNILib', 'JGetFreeDiskSpace', '()F')
    utils_binding = ('com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils', 'getFreeSpace', '(Ljava/lang/String;)F')
    assert methods[native_binding] == methods[utils_binding] == symbols['h_space_kb']

    # Apply only the relocation consumed by the warning and initialize its
    # static guards/cache; platform object construction is outside this test.
    assert word(GAME + 0x133a7a4) == 0x134f4e0
    store(GAME + 0x133a7a4, GAME + 0x134f4e0)
    mu.mem_write(GAME + 0x134f4e0, b'\1')
    store(GAME + 0x134f7cc, 1)
    store(GAME + 0x134f72c + 8, 0)
    store(GAME + 0x1350428 + 8, HEAP + 0x3000)

    def code(uc, address, size, data):
        if address == symbols['tasm2_sd_space']:
            state['sd_queries'] += 1
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0), struct.pack('<Q', state['available']))
            if uc.reg_read(UC_ARM_REG_R1):
                uc.mem_write(uc.reg_read(UC_ARM_REG_R1), struct.pack('<Q', 512 << 30))
            uc.reg_write(UC_ARM_REG_R0, 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == GAME + FLOAT_GETTER:
            state['float_queries'] += 1
            bits = struct.unpack('<I', struct.pack('<f', state['callback_kb']))[0]
            uc.reg_write(UC_ARM_REG_R0, bits)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == symbols['debugPrintf']:
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif state['stop_predicate'] and address in (GAME + WARNING, GAME + ENOUGH):
            uc.emu_stop()

    def invalid(uc, access, address, size, value, data):
        raise AssertionError(f"Unmapped test memory {address:#x}, PC={uc.reg_read(UC_ARM_REG_PC):#x}")

    mu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    mu.hook_add(UC_HOOK_CODE, code)

    def run(address, r0=0, r1=0):
        mu.reg_write(UC_ARM_REG_SP, stack_top)
        mu.reg_write(UC_ARM_REG_LR, RETURN)
        mu.reg_write(UC_ARM_REG_R0, r0)
        mu.reg_write(UC_ARM_REG_R1, r1)
        try:
            mu.emu_start(address, RETURN, count=10000)
        except Exception as error:
            raise AssertionError(f"Test execution at {mu.reg_read(UC_ARM_REG_PC):#x}, r0={mu.reg_read(UC_ARM_REG_R0):#x}, r4={mu.reg_read(UC_ARM_REG_R4):#x}, r6={mu.reg_read(UC_ARM_REG_R6):#x}, r7={mu.reg_read(UC_ARM_REG_R7):#x}") from error

    # The 735 hardware log reports 3881.875 MiB. Supplying MiB to this native
    # consumer reproduces a 3 MiB result and hence its 256 MiB warning branch.
    state['callback_kb'] = 3881.875
    run(GAME + CAPACITY)
    assert mu.reg_read(UC_ARM_REG_R0) == 3
    state['stop_predicate'] = True
    run(GAME + ENTRY, object_pointer)
    assert mu.reg_read(UC_ARM_REG_PC) == GAME + WARNING
    state['stop_predicate'] = False

    for mb in (0, 255, 256, 257, 3881.875, 20608, 307200):
        state['available'] = int(mb * 1048576)
        run(methods[native_binding], result_buffer)
        assert mu.reg_read(UC_ARM_REG_PC) == RETURN
        value = struct.unpack('<f', bytes(mu.mem_read(result_buffer, 4)))[0]
        assert value == mb * 1024, (mb, value)
        assert word(result_buffer + 4) == 0
        state['callback_kb'] = value
        run(GAME + CAPACITY)
        assert mu.reg_read(UC_ARM_REG_PC) == RETURN
        assert mu.reg_read(UC_ARM_REG_R0) == int(mb)
        state['stop_predicate'] = True
        run(GAME + ENTRY, object_pointer)
        assert mu.reg_read(UC_ARM_REG_PC) == GAME + (WARNING if mb < 256 else ENOUGH)
        state['stop_predicate'] = False

    # Execute the linked production patch against the original game image.
    before = bytes(mu.mem_read(GAME + ENTRY, 16))
    run(symbols['tasm2_disable_space_warning'], GAME, 0x13b8000)
    assert mu.reg_read(UC_ARM_REG_PC) == RETURN and mu.reg_read(UC_ARM_REG_R0) == 0
    assert word(GAME + ENTRY) == 0xe12fff1e
    assert bytes(mu.mem_read(GAME + ENTRY + 4, 12)) == before[4:]
    saved = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
             UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
    for mb in (0, 1, 255, 256, 3881.875, 307200):
        state['callback_kb'] = mb * 1024
        calls = state['float_queries']
        snapshot = bytes(mu.mem_read(object_pointer, 256))
        for n, reg in enumerate(saved):
            mu.reg_write(reg, 0x6000 + n)
        run(GAME + ENTRY, object_pointer)
        assert mu.reg_read(UC_ARM_REG_PC) == RETURN
        assert state['float_queries'] == calls, 'Bypassed routine still queried capacity'
        assert bytes(mu.mem_read(object_pointer, 256)) == snapshot
        assert mu.reg_read(UC_ARM_REG_SP) == stack_top
        assert all(mu.reg_read(reg) == 0x6000 + n for n, reg in enumerate(saved))
    print('warning native: old MB handler reproduces warning; registered production KiB handler + original conversion pass 256 MiB boundaries and >4 GiB')
    print('warning bypass: linked production patch validates original ELF, returns without capacity query/modal/state writes and preserves stack/callee-saved registers')
    print('Horizon/platform callbacks are mocked; console loading beyond the modal is still a hardware retest.')


if __name__ == '__main__':
    main()
