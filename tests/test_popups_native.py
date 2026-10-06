#!/usr/bin/env python3
"""Exercise the linked popup patch/queue and original 1.2.8d continuations.

No game data is bundled. Requires the privately supplied original native ELF,
Unicorn and pyelftools. Mutex and UI-manager virtual services are mocked; the
production patcher, ARM stubs, queue and native acknowledgement code execute.
"""
import argparse
import hashlib
import re
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import *
from test_space_native import load, LIBRARY_SHA256, WRAPPER, GAME, HEAP, STACK, RETURN


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('payload', type=Path)
    ap.add_argument('library', type=Path)
    args = ap.parse_args()
    assert hashlib.sha256(args.library.read_bytes()).hexdigest() == LIBRARY_SHA256
    root = Path(__file__).resolve().parent.parent
    header = (root / 'source/tasm2_popup_sites.h').read_text(encoding='utf8')
    sites = [(int(pc, 16), int(key, 16), int(mode)) for pc, key, mode in
             re.findall(r'\{0x([0-9a-f]+)u, 0x([0-9a-f]+)u, (\d),', header)]
    assert len(sites) == 93
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    symbols = load(mu, args.payload, WRAPPER)
    # The real payload CRT rebases static C pointer tables before port_load.
    # Reproduce those relative relocations; leaving the key pointers unrebased
    # would test an uninitialized wrapper instead of the linked production code.
    with args.payload.open('rb') as stream:
        payload = ELFFile(stream)
        for section in payload.iter_sections():
            if not hasattr(section, 'iter_relocations') or not section['sh_flags'] & 2:
                continue
            for relocation in section.iter_relocations():
                assert relocation['r_info_type'] == 23, relocation.entry
                address = WRAPPER + relocation['r_offset']
                value = struct.unpack('<I', bytes(mu.mem_read(address,4)))[0]
                mu.mem_write(address,struct.pack('<I',(value+WRAPPER)&0xffffffff))
    load(mu, args.library, GAME)
    for base, size in ((HEAP, 0x10000), (STACK, 0x10000), (RETURN, 4096)):
        mu.mem_map(base, size)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    top, userdata = STACK + 0xf000, HEAP + 0x800
    synthetic_cb, set_state = RETURN + 0x100, RETURN + 0x140
    events, ui_states = [], []
    locks = {symbols[k] for k in ('mutexLock', 'mutexUnlock') if k in symbols}
    def word(a): return struct.unpack('<I', bytes(mu.mem_read(a, 4)))[0]
    def store(a, v): mu.mem_write(a, struct.pack('<I', v))
    def code(uc, address, size, data):
        if address in locks or address == symbols['debugPrintf']:
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == symbols.get('fatal_error'):
            raise AssertionError('Unexpected fatal error')
        elif address == synthetic_cb:
            events.append((uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)))
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == GAME + 0x13bff8:
            uc.reg_write(UC_ARM_REG_R0, HEAP + 0x4000)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == set_state:
            ui_states.append(uc.reg_read(UC_ARM_REG_R1))
            uc.reg_write(UC_ARM_REG_R0, 1)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    def invalid(uc, access, address, size, value, data):
        raise AssertionError(f'Unmapped {address:#x}, PC={uc.reg_read(UC_ARM_REG_PC):#x}')
    mu.hook_add(UC_HOOK_CODE, code)
    mu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    def run(address, r0=0, r1=0, r2=0, r3=0, end=RETURN):
        mu.reg_write(UC_ARM_REG_SP, top)
        mu.reg_write(UC_ARM_REG_LR, RETURN)
        for reg, value in ((UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),
                           (UC_ARM_REG_R2,r2),(UC_ARM_REG_R3,r3)):
            mu.reg_write(reg,value)
        mu.emu_start(address, end, count=200000)
        assert mu.reg_read(UC_ARM_REG_PC) == end, f'Did not return: {mu.reg_read(UC_ARM_REG_PC):#x}'
        return mu.reg_read(UC_ARM_REG_R0)

    # No absolute/relocated pointer or external ARM B/BL enters reused bytes.
    with args.library.open('rb') as stream:
        elf = ELFFile(stream)
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        for segment in segments:
            data = segment.data()
            for off in range(0, len(data)-3, 4):
                value = struct.unpack_from('<I', data, off)[0]
                assert not 0x3f2034 <= value < 0x3f2084, 'Pointer into reused body'
        for section in elf.iter_sections():
            if not section['sh_flags'] & 4:
                continue
            data = section.data()
            for off in range(0, len(data)-3, 4):
                pc = section['sh_addr'] + off
                value = struct.unpack_from('<I', data, off)[0]
                if value & 0x0e000000 != 0x0a000000:
                    continue
                displacement = value & 0xffffff
                if displacement & 0x800000: displacement -= 0x1000000
                target = (pc+8+displacement*4) & 0xffffffff
                if 0x3f2034 <= target < 0x3f2084:
                    assert 0x3f2030 <= pc < 0x3f2274, 'External branch into reused body'
    image_size = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
    # Mapped library is larger than file-backed text; no player state is needed.
    original = bytes(mu.mem_read(GAME, image_size))
    assert run(symbols['tasm2_disable_space_warning'], GAME, image_size) == 0
    prepatched = bytes(mu.mem_read(GAME, image_size))
    patch = symbols['tasm2_popups_patch']
    assert run(patch, GAME, 0x1352321, symbols['tasm2_popups_enqueue']) == 0xffffffff
    guards = [0x3f2030, 0x3f2034, 0x3f2080, 0x20e48c, 0x20ea3c, 0x20c5f0,
              0x433f48,0x433f50,0x433790,0x433ee4,0x433efc]
    guards += [pc - 12 for pc, key, mode in sites] + [pc for pc, key, mode in sites] + [key for pc, key, mode in sites]
    for offset in sorted(set(guards)):
        previous = bytes(mu.mem_read(GAME + offset, 1))
        mu.mem_write(GAME + offset, bytes([previous[0] ^ 1]))
        cave_before = bytes(mu.mem_read(GAME + 0x3f2034, 80))
        calls_before = {pc: word(GAME + pc) for pc, key, mode in sites}
        gate_before = word(GAME+0x433f50)
        assert run(patch, GAME, image_size, symbols['tasm2_popups_enqueue']) == 0xffffffff
        assert bytes(mu.mem_read(GAME + 0x3f2034, 80)) == cave_before, 'Partial stub publication'
        for pc, key, mode in sites:
            assert word(GAME + pc) == calls_before[pc], 'Partial call-site publication'
        assert word(GAME+0x433f50) == gate_before, 'Partial gate publication'
        mu.mem_write(GAME + offset, previous)
    assert run(patch, GAME, image_size, symbols['tasm2_popups_enqueue']) == 0
    modified = bytes(mu.mem_read(GAME, image_size))
    permitted = {i for i in range(0x3f2034, 0x3f2084)}
    permitted.update(range(0x433f50,0x433f54))
    for pc, key, mode in sites: permitted.update(range(pc,pc+4))
    assert all(i in permitted for i,(a,b) in enumerate(zip(prepatched,modified)) if a != b)
    # Representative gameplay/retry/abandon/reset/purchase/data-conflict sites.
    for pc in (0x217dd8,0x217e6c,0x217f00,0x233144,0x391b20,0x43dad8,
               0x46099c,0x460a58,0x49138c,0x497d08,0x4a130c,0x4a1c8c):
        assert modified[pc:pc+4] == original[pc:pc+4]
    run(symbols['tasm2_popups_init'])
    saved_regs = (UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
                  UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)
    for index,(pc,key,mode) in enumerate(sites):
        store(top, userdata if mode == 3 else synthetic_cb)
        store(top+4, userdata)
        for i,reg in enumerate(saved_regs): mu.reg_write(reg,0x70000100+i)
        before_events = len(events)
        run(GAME+pc, r3=synthetic_cb if mode == 3 else 0, end=GAME+pc+4)
        assert len(events) == before_events, 'Callback ran inside builder call'
        assert mu.reg_read(UC_ARM_REG_SP) == top
        assert [mu.reg_read(r) for r in saved_regs] == [0x70000100+i for i in range(8)]
        run(symbols['tasm2_popups_tick'])
        if mode != 4:
            assert events[-1] == (0 if mode == 3 else mode,userdata)
            assert len(events) == before_events+1
        else: assert len(events) == before_events
        run(symbols['tasm2_popups_tick'])
        assert len(events) == before_events + (mode != 4)

    # Execute original native continuations through patched call -> queue -> tick.
    store(GAME+0x133a728,GAME+0x134edb8)
    store(GAME+0x134edb8,HEAP+0x4000)
    store(HEAP+0x4000,HEAP+0x4100)
    store(HEAP+0x4100+12,set_state)
    def acknowledge(pc,cb,details=False):
        store(top,userdata if details else GAME+cb)
        store(top+4,userdata)
        run(GAME+pc,r3=GAME+cb if details else 0,end=GAME+pc+4)
        run(symbols['tasm2_popups_tick'])
    # Hardware build 907 ran the FIRST_CHECK callback hundreds of times. Each
    # invocation resets the global UI to state 2, interrupting Play/Settings.
    # The prior isolated one-ack test missed this repeated-frame behavior.
    for frame in range(3):
        mu.mem_write(GAME+0x1352321,b'\1')
        run(GAME+0x42c2fc,r1=userdata)
        assert ui_states[-1] == 2
    # The current first-check display call is a NOP and queues no callback.
    # Its requestor now goes to the game's existing menu dispatch before
    # setting the pending byte or testing network/cloud state.
    baseline = len(ui_states)
    acknowledge(0x435a80,0x42c2fc)
    assert len(ui_states) == baseline
    gate_original = original[0x433f50:0x433f54]
    gate_fixed = bytes(mu.mem_read(GAME+0x433f50,4))
    native_game = HEAP+0x5000
    mu.mem_write(native_game+0xb52,b'\0')
    mu.mem_write(GAME+0x433f50,gate_original)
    mu.reg_write(UC_ARM_REG_R4,userdata)
    run(GAME+0x433f48,r3=native_game,end=GAME+0x433f54)
    mu.mem_write(GAME+0x433f50,gate_fixed)
    destinations = [0x435840,0x4359e4,0x4358c4,0x4359e4,0x435764,0x4359e4,
                    0x4357a0,0x4359e4,0x4357f4,0x4358fc,0x4359e4,0x435828,
                    0x4359a8,0x4359e4,0x4359e4,0x43594c,0x4359e4,0x435970]
    # Exercise the actual ARM condition and dispatch on repeated frames, for
    # both server-ready byte values. Every native pending menu action keeps
    # its original branch destination; no queued callback resets the scene.
    for ready in (0,1):
        mu.mem_write(native_game+0xb52,bytes([ready]))
        for pending in (0,*range(2,20),99):
            target = 0x434f08 if pending == 0 else \
                     destinations[pending-2] if pending < 20 else 0x4359e4
            for frame in range(4):
                store(userdata+0x74,pending)
                mu.mem_write(GAME+0x1352321,b'\0')
                mu.reg_write(UC_ARM_REG_R4,userdata)
                run(GAME+0x433f48,r3=native_game,end=GAME+target)
                assert word(userdata+0x74) == pending
                assert bytes(mu.mem_read(GAME+0x1352321,1)) == b'\0'
                run(symbols['tasm2_popups_tick'])
                assert len(ui_states) == baseline
    store(userdata+0x8c,12)
    acknowledge(0x22ac40,0x1cca6c)
    assert word(userdata+0x8c) == 13
    mu.mem_write(GAME+0x133c120,b'\1')
    acknowledge(0x22aa30,0x1d1a80)
    assert bytes(mu.mem_read(GAME+0x133c120,1)) == b'\0'
    store(userdata+0x8c,10)
    acknowledge(0x22ab90,0x1d6658,True)
    assert word(userdata+0x8c) == 11
    store(userdata+0x264,2);store(userdata+0x268,77)
    acknowledge(0x491e4c,0x491bd0)
    assert word(userdata+0x264) == 6 and word(userdata+0x268) == 0
    print('popups ARM: 93 private-ELF call sites, atomic guards, ARM ABI/register preservation, deferred once-only choice/userdata and unchanged gameplay dialogs pass')
    print('popups native: old FIRST_CHECK repeats UI state-2 reset; fixed requestor preserves all 18 native menu actions over repeated frames without reset; cloud 12->13, update skip/details 10->11 and upload decline pass')


if __name__ == '__main__':
    main()
