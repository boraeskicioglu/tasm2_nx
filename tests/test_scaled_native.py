#!/usr/bin/env python3
"""Execute original scaled game time, production ARM cave and compiled carry.
Requires the owner's original 1.2.8d library; no game bytes are bundled.
"""
import argparse, hashlib, struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
from test_space_native import load, WRAPPER, GAME, HEAP, STACK, RETURN, LIBRARY_SHA256

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('payload',type=Path);p.add_argument('library',type=Path);a=p.parse_args()
    assert hashlib.sha256(a.library.read_bytes()).hexdigest()==LIBRARY_SHA256
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM);syms=load(u,a.payload,WRAPPER);load(u,a.library,GAME)
    # Reproduce the payload CRT's relative relocation pass before calling C.
    with a.payload.open('rb') as f:
        elf=ELFFile(f)
        for sec in elf.iter_sections():
            if not hasattr(sec,'iter_relocations') or not sec['sh_flags']&2:continue
            for rel in sec.iter_relocations():
                assert rel['r_info_type']==23
                at=WRAPPER+rel['r_offset']
                v=struct.unpack('<I',u.mem_read(at,4))[0]
                u.mem_write(at,struct.pack('<I',(v+WRAPPER)&0xffffffff))
    for base in (HEAP,STACK,RETURN):u.mem_map(base,0x10000)
    u.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    def put(at,v):u.mem_write(at,struct.pack('<I',v&0xffffffff))
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
    size=next(end-begin+1 for begin,end,_ in u.mem_regions() if begin==GAME)
    def call(at,r0=0,r1=0,r2=0):
        u.reg_write(UC_ARM_REG_SP,STACK+0xf000);u.reg_write(UC_ARM_REG_LR,RETURN)
        for reg,v in [(UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),(UC_ARM_REG_R2,r2)]:u.reg_write(reg,v)
        u.emu_start(at,RETURN,count=20000)
        assert u.reg_read(UC_ARM_REG_PC)==RETURN
        return u.reg_read(UC_ARM_REG_R0)
    def hook(uc,at,size,data):
        if at==syms['debugPrintf']:uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    u.hook_add(UC_HOOK_CODE,hook)
    def tick(elapsed,scale):
        sentinels=[(UC_ARM_REG_R0,0x111),(UC_ARM_REG_R1,0x222),(UC_ARM_REG_R2,0x333),
                   (UC_ARM_REG_R5,0x555),(UC_ARM_REG_R6,0x666),(UC_ARM_REG_R7,0x777),
                   (UC_ARM_REG_R9,0x999),(UC_ARM_REG_R10,0xaaa),(UC_ARM_REG_R11,0xbbb),
                   (UC_ARM_REG_R12,0xccc),(UC_ARM_REG_LR,RETURN)]
        for reg,v in sentinels:u.reg_write(reg,v)
        u.reg_write(UC_ARM_REG_R8,HEAP);u.reg_write(UC_ARM_REG_R4,0)
        u.reg_write(UC_ARM_REG_SP,STACK+0xf000)
        u.reg_write(UC_ARM_REG_S16,bits(elapsed));u.reg_write(UC_ARM_REG_S15,bits(scale))
        u.reg_write(UC_ARM_REG_S17,0x12345678)
        u.emu_start(GAME+0x3e9154,GAME+0x3e9180,count=20000)
        assert u.reg_read(UC_ARM_REG_PC)==GAME+0x3e9180
        assert u.reg_read(UC_ARM_REG_SP)==STACK+0xf000
        assert u.reg_read(UC_ARM_REG_S17)==0x12345678
        for reg,v in sentinels:assert u.reg_read(reg)==v,(reg,hex(u.reg_read(reg)),hex(v))
        return word(HEAP+0x320)
    assert sum(tick(16+(i%3==0),.05) for i in range(120))==0, 'Original stalls in slow motion'
    assert call(syms['tasm2_disable_space_warning'],GAME,size)==0
    original=bytes(u.mem_read(GAME,size));patch=syms['tasm2_patch_scaled_tick']
    for guard in (0x3f2030,0x3f2084,0x3f20a8,0x3e9154,0x3e9168,0x3e917c):
        u.mem_write(GAME,original);put(GAME+guard,0)
        before=bytes(u.mem_read(GAME,size))
        assert call(patch,GAME,size,syms['tasm2_scaled_tick'])==0xffffffff
        assert bytes(u.mem_read(GAME,size))==before
    u.mem_write(GAME,original)
    assert call(patch,GAME,size,syms['tasm2_scaled_tick'])==0
    after=bytes(u.mem_read(GAME,size))
    changed=[i for i,(x,y) in enumerate(zip(original,after)) if x!=y]
    assert all(0x3e9158<=i<0x3e915c or 0x3f2084<=i<0x3f20ac for i in changed)
    for scale in (.01,.05,.1,.5,1,2):
        put(syms['scaled_remainder'],0)
        total=sum(tick(16+(i%3==0),scale) for i in range(600))
        expected=sum(16+(i%3==0) for i in range(600))*scale
        assert abs(total-expected)<1.1,(scale,total,expected)
    put(syms['scaled_remainder'],0)
    assert tick(.75,1)==0
    for i in range(100):assert tick(17,0)==0 # true pause doesn't invent time
    assert tick(.5,1)==1
    assert tick(500,1)==100
    assert tick(17,-1)==0
    assert tick(16,float('nan'))==0
    assert tick(17,1)==17
    # Expanded dead-body safety check for the new cave.
    with a.library.open('rb') as f:
        elf=ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type']!='PT_LOAD':continue
            data=seg.data()
            for off in range(0,len(data)-3,4):
                value=struct.unpack_from('<I',data,off)[0]
                assert not 0x3f2084<=value<0x3f20ac, 'Pointer into cave'
        for sec in elf.iter_sections():
            if not sec['sh_flags']&4:continue
            data=sec.data()
            for off in range(0,len(data)-3,4):
                pc=sec['sh_addr']+off;v=struct.unpack_from('<I',data,off)[0]
                if v&0x0e000000!=0x0a000000:continue
                d=v&0xffffff
                if d&0x800000:d-=0x1000000
                target=(pc+8+d*4)&0xffffffff
                if 0x3f2084<=target<0x3f20ac:assert 0x3f2030<=pc<0x3f2274
    print('Scaled time: original zero-progress reproduced; 3600 60 Hz slow/normal updates retain real time, pauses and clamp; ABI/preimages/cave checked: PASS')
if __name__=='__main__':main()
