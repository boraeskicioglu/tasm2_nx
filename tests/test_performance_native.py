#!/usr/bin/env python3
"""Run linked performance patches and original native allocation/RTA consumers.
Only heap/GL-config/string-release services are mocked; actual 1.2.8d ARM/VFP
allocation selection, zero-fill, free selection and dimension arithmetic run.
"""
import argparse, hashlib, struct, math
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import *
from test_space_native import load, WRAPPER, GAME, HEAP, STACK, RETURN, LIBRARY_SHA256

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('payload',type=Path);p.add_argument('library',type=Path);a=p.parse_args()
    assert hashlib.sha256(a.library.read_bytes()).hexdigest()==LIBRARY_SHA256
    u=Uc(UC_ARCH_ARM, UC_MODE_ARM)
    syms=load(u,a.payload,WRAPPER);load(u,a.library,GAME)
    for base in (HEAP,STACK,RETURN):u.mem_map(base,0x10000)
    u.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    size=next(e-b+1 for b,e,_ in u.mem_regions() if b==GAME)
    def put(at,n):u.mem_write(at,struct.pack('<I',n&0xffffffff))
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
    def reg(r):return u.reg_read(r)
    def ret(n=0):u.reg_write(UC_ARM_REG_R0,n);u.reg_write(UC_ARM_REG_PC,reg(UC_ARM_REG_LR))
    # PIE globals/callback pointers need the same relative relocations as the
    # real loader; standalone leaf patch tests previously needed no globals.
    with a.payload.open('rb') as stream:
        elf=ELFFile(stream)
        for section in elf.iter_sections():
            if not hasattr(section,'iter_relocations') or not section['sh_flags'] & 2:continue
            table=elf.get_section(section['sh_link'])
            for relocation in section.iter_relocations():
                at=WRAPPER+relocation['r_offset'];kind=relocation['r_info_type']
                symbol=table.get_symbol(relocation['r_info_sym'])
                if kind==23:put(at,word(at)+WRAPPER)
                elif kind in (21,22) and symbol['st_value']:put(at,WRAPPER+symbol['st_value'])
    state=dict(malloc=0,zero=0,mmap=0,free=0,mmap_free=0,config=0,release=0)
    flag=GAME+0x134f4e0
    guard=GAME+0x1a1d30+8+word(GAME+0x1a1dbc)
    put(guard,1)
    # native MMapAllocator construction marker's GOT slot (free checks it).
    put(GAME+0x133a70c, HEAP+0x600);u.mem_write(HEAP+0x600,b'\x01')
    def hook(uc,at,n,_):
        if at in (syms['debugPrintf'],syms['mutexLock'],syms['mutexUnlock']):ret()
        elif at==syms['fatal_error']:raise AssertionError('Combined native fix preparation rejected original ELF')
        elif at==GAME+0xa5a20:
            state['malloc']+=1;u.mem_write(HEAP+0x1000,b'\xcc'*reg(UC_ARM_REG_R0));ret(HEAP+0x1000)
        elif at==GAME+0xa5a44:
            state['zero']+=1;u.mem_write(reg(UC_ARM_REG_R0),bytes([reg(UC_ARM_REG_R1)&255])*reg(UC_ARM_REG_R2));ret(reg(UC_ARM_REG_R0))
        elif at==GAME+0x4d9104:state['mmap']+=1;ret(HEAP+0x2000)
        elif at==GAME+0xa5ae0:state['free']+=1;assert reg(UC_ARM_REG_R0)==HEAP+0x1000;ret()
        elif at==GAME+0x4d8bf4:state['mmap_free']+=1;ret(1)
        elif at==GAME+0x61a08c:state['config']+=1;ret(1)
        elif at==GAME+0x61a09c:state['config']+=1;ret(bits(1.0))
        elif at==GAME+0x11272fc:state['release']+=1;ret()
    u.hook_add(UC_HOOK_CODE,hook)
    def invalid(uc,access,at,n,value,_):
        raise AssertionError(('Unmapped access',hex(at),'PC',hex(reg(UC_ARM_REG_PC)),'LR',hex(reg(UC_ARM_REG_LR))))
    u.hook_add(UC_HOOK_MEM_INVALID,invalid)
    def call(at,r0=0,r1=0,r2=0,stop=RETURN):
        u.reg_write(UC_ARM_REG_SP,STACK+0xf000);u.reg_write(UC_ARM_REG_LR,RETURN)
        for r,v in ((UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),(UC_ARM_REG_R2,r2)):u.reg_write(r,v)
        try:u.emu_start(at,stop,count=500000)
        except Exception as err:raise AssertionError((hex(at),hex(reg(UC_ARM_REG_PC)),hex(reg(UC_ARM_REG_LR)),hex(reg(UC_ARM_REG_R0)),hex(reg(UC_ARM_REG_R1)))) from err
        assert reg(UC_ARM_REG_PC)==stop,hex(reg(UC_ARM_REG_PC))
        return reg(UC_ARM_REG_R0)
    def init_flag():
        u.reg_write(UC_ARM_REG_SP,STACK+0xe000)
        u.reg_write(UC_ARM_REG_R7,GAME+0x133b9e8)
        put(GAME+0x133a7a4,flag);put(STACK+0xe850,HEAP+0x800)
        u.emu_start(GAME+0x3fd9fc,GAME+0x3fda20,count=100)
        return u.mem_read(flag,1)[0]
    def scale_dimensions(w,h):
        u.reg_write(UC_ARM_REG_R10,w);u.reg_write(UC_ARM_REG_R9,h)
        put(STACK+0xf088,HEAP+0x900)
        call(GAME+0x3fbcac,stop=GAME+0x3fbd20)
        return reg(UC_ARM_REG_R10),reg(UC_ARM_REG_R9)
    pristine=bytes(u.mem_read(GAME,size))
    assert init_flag()==1
    assert call(GAME+0x1a1cb4,512,0x400)==HEAP+0x2000 and state['mmap']==1
    assert scale_dimensions(1280,720)==(1280,720)
    u.mem_write(GAME,pristine)
    # Restore runtime relocated pointers/fake config and allocator guards.
    put(guard,1);put(GAME+0x133a70c,HEAP+0x600)
    before=bytes(u.mem_read(GAME,size))
    assert call(syms['tasm2_performance_patch'],GAME,size,bits(.8))==0
    changed=bytes(u.mem_read(GAME,size))
    assert all(0x3fda08<=i<0x3fda0c or 0x3fbcac<=i<0x3fbcb8 for i,(x,y) in enumerate(zip(before,changed)) if x!=y)
    assert init_flag()==0
    configs=state['config'];mmaps=state['mmap'];frees=state['free']
    for flags in (0,1,0x400,0x401):
        for n in (1,16,127,512,4096):
            ptr=call(GAME+0x1a1cb4,n,flags)
            assert ptr==HEAP+0x1000 and u.mem_read(ptr,n)==bytes(n)
            call(GAME+0x1a5a48,ptr)
    assert state['mmap']==mmaps and state['mmap_free']==0 and state['free']==frees+20
    call(GAME+0x1a5a48,0);assert state['free']==frees+20
    for w,h,out in ((1280,720,(1024,576)),(1920,1080,(1536,864)),(960,540,(768,432))):
        assert scale_dimensions(w,h)==out
    assert state['config']==configs,'Patched named init/RTA reads must not call original config getter'
    for f in (.75,.80,.85,1.0):
        u.mem_write(GAME,pristine)
        assert call(syms['tasm2_performance_patch'],GAME,size,bits(f))==0
        expected=tuple(math.trunc(struct.unpack('<f',struct.pack('<f',v*struct.unpack('<f',struct.pack('<f',f))[0]))[0]) for v in (1280,720))
        assert scale_dimensions(1280,720)==expected,(f,expected)
    for f in (0,.749,1.001,-1,float('nan'),float('inf')):
        u.mem_write(GAME,pristine)
        assert call(syms['tasm2_performance_patch'],GAME,size,bits(f))==0xffffffff
        assert bytes(u.mem_read(GAME,size))==pristine
    for bad in (0x3fd9fc,0x3fda08,0x1a1d18,0x1a5a54,0x3fbcac,0x3fbcf8,0x11aac14,0x11a4824):
        u.mem_write(GAME,pristine);put(GAME+bad,0)
        bad_image=bytes(u.mem_read(GAME,size))
        assert call(syms['tasm2_performance_patch'],GAME,size,bits(.8))==0xffffffff
        assert bytes(u.mem_read(GAME,size))==bad_image
    for ptr,n in ((0,size),(GAME,0x3fda0c),(GAME,0x11aac24)):
        u.mem_write(GAME,pristine)
        assert call(syms['tasm2_performance_patch'],ptr,n,bits(.8))==0xffffffff
        assert bytes(u.mem_read(GAME,size))==pristine
    # Exercise the loader's real combined preparation, including the existing
    # warning-body code caves and checkpoint/Shop patches. Platform locks/log
    # are mocked, but all checked patch helpers and bindings execute together.
    u.mem_write(GAME,pristine)
    module=HEAP+0x5000;u.mem_write(module,bytes(1024))
    put(module+12,GAME);put(module+16,GAME);put(module+24,size)
    call(syms['tasm2_fixes_prepare'],module)
    assert init_flag()==0 and scale_dimensions(1280,720)==(1024,576)
    assert word(GAME+0x3f2030)==0xe12fff1e  # earlier space-warning bypass
    assert word(GAME+0x547384)==0xe1a00000  # first offline SaveIndex
    assert word(GAME+0x3b1be4)==0xea00001f  # retained real-time 60 Hz gate
    print('Combined loader fix preparation: native performance, save, Shop, warning/popup and finisher patches coexist: PASS')
    print('Performance: original disk allocator selected before patch; compiled patch selects original zero-filled heap/free for 20 allocation cases; native VFP RTA dimensions verified at .75/.80/.85/1.0; only four instructions changed; all layout/scale/size guards fail without writes: PASS')
if __name__=='__main__':main()
