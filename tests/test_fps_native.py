#!/usr/bin/env python3
"""Execute compiled FPS patch and original 1.2.8d frame timer in ARM emulation.
Only clock and sleep services are mocked; elapsed/clamping code stays native.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from test_space_native import load, WRAPPER, GAME, HEAP, STACK, RETURN, LIBRARY_SHA256

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('payload', type=Path); p.add_argument('library', type=Path)
    a = p.parse_args()
    assert hashlib.sha256(a.library.read_bytes()).hexdigest() == LIBRARY_SHA256
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    syms = load(u, a.payload, WRAPPER); load(u, a.library, GAME)
    for base in (HEAP, STACK, RETURN): u.mem_map(base, 0x10000)
    mapped_size = next(end - begin + 1 for begin, end, _ in u.mem_regions() if begin == GAME)
    state = {'clock': 0, 'waits': 0}
    def put(addr, v): u.mem_write(addr, struct.pack('<I', v & 0xffffffff))
    def word(addr): return struct.unpack('<I', u.mem_read(addr, 4))[0]
    def hook(uc, addr, size, data):
        if addr == GAME + 0x8161d8:
            uc.reg_write(UC_ARM_REG_R0, state['clock'] & 0xffffffff)
            uc.reg_write(UC_ARM_REG_R1, state['clock'] >> 32)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif addr == GAME + 0xa7eae4:
            state['clock'] += uc.reg_read(UC_ARM_REG_R0); state['waits'] += 1
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    u.hook_add(UC_HOOK_CODE, hook)
    def call(addr, r0, r1, r2=0):
        u.reg_write(UC_ARM_REG_SP, STACK+0xf000); u.reg_write(UC_ARM_REG_LR, RETURN)
        for reg, v in [(UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),(UC_ARM_REG_R2,r2)]: u.reg_write(reg,v)
        u.emu_start(addr, RETURN, count=20000)
        assert u.reg_read(UC_ARM_REG_PC) == RETURN
        return u.reg_read(UC_ARM_REG_R0)
    def timer(limit, elapsed, active=1):
        state['clock'] = 10000+elapsed; state['waits'] = 0
        put(HEAP+0x328,limit); put(HEAP+0x338,10000); put(HEAP+0x33c,0)
        call(GAME+0x3b1bac,HEAP,active)
        return word(HEAP+0x324),state['waits']
    assert timer(33,5) == (33,1), 'Original native FPSLimit must wait'
    before = bytes(u.mem_read(GAME,mapped_size))
    assert call(syms['tasm2_unlock_fps'],GAME,mapped_size) == 0
    after = bytes(u.mem_read(GAME,mapped_size))
    assert [i for i,(x,y) in enumerate(zip(before,after)) if x != y] == [0x3b1be7]
    for limit in (-1,0,16,33,100):
        for active in (0,1):
            for elapsed in (-4,0,5,16,17,30,33,50,99,100,200):
                delta, waits = timer(limit,elapsed,active)
                assert delta == max(0,min(100,elapsed)) and waits == 0, (limit,active,elapsed,delta,waits)
    # Every repeated frame remains paced only by the wrapper's EGL presentation.
    for i in range(300): assert timer(33,16+(i%3==0)) == (16+(i%3==0),0)
    u.mem_write(GAME, before)
    put(GAME+0x3b1bd0,0)
    corrupted = bytes(u.mem_read(GAME,mapped_size))
    assert call(syms['tasm2_unlock_fps'],GAME,mapped_size) == 0xffffffff
    assert bytes(u.mem_read(GAME,mapped_size)) == corrupted
    print('FPS: original 33ms wait verified; production patch skips only cap sleep; real elapsed/clamp preserved over 410 cases; altered native layout rejected: PASS')
if __name__ == '__main__': main()
