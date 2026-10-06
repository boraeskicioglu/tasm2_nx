#!/usr/bin/env python3
"""Execute checked Shop init call, CRM async/local branch and native Shop gate.
Only platform connectivity and unrelated string/service construction are mocked.
Requires linked payload and owner's hash-checked original 1.2.8d ELF.
"""
import argparse,hashlib,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
from test_space_native import load,WRAPPER,GAME,HEAP,STACK,RETURN,LIBRARY_SHA256

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('payload',type=Path);p.add_argument('library',type=Path);a=p.parse_args()
    assert hashlib.sha256(a.library.read_bytes()).hexdigest()==LIBRARY_SHA256
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM);symbols=load(u,a.payload,WRAPPER);load(u,a.library,GAME)
    for base in (HEAP,STACK,RETURN):u.mem_map(base,0x10000)
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    def put(at,v):u.mem_write(at,struct.pack('<I',v&0xffffffff))
    # Match loader relocation for the payload tables and game's stack guard.
    for path,base in [(a.payload,WRAPPER),(a.library,GAME)]:
        with path.open('rb') as f:
            elf=ELFFile(f)
            for sec in elf.iter_sections():
                if not hasattr(sec,'iter_relocations') or not sec['sh_flags']&2:continue
                table=elf.get_section(sec['sh_link'])
                for rel in sec.iter_relocations():
                    at=base+rel['r_offset'];t=rel['r_info_type'];sym=table.get_symbol(rel['r_info_sym'])
                    if t==23:put(at,word(at)+base)
                    elif sym['st_value']:put(at,base+sym['st_value']+(word(at) if t==2 else 0))
                    else:put(at,RETURN+0x2000)
    size=next(end-begin+1 for begin,end,_ in u.mem_regions() if begin==GAME)
    original=bytes(u.mem_read(GAME,size));state={'capture':False,'args':None}
    def hook(uc,at,size,data):
        if at==GAME+0xc59d78 and state['capture']:
            state['args']=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
            uc.emu_stop()
        elif at==GAME+0x1128540 or at==symbols['debugPrintf']:
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        elif at==GAME+0x149084:
            uc.reg_write(UC_ARM_REG_R0,0) # actual port advertises offline
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    u.hook_add(UC_HOOK_CODE,hook)
    def setup():
        u.reg_write(UC_ARM_REG_SP,STACK+0xf000);u.reg_write(UC_ARM_REG_LR,RETURN)
    def call(addr,r0=0,r1=0):
        setup();u.reg_write(UC_ARM_REG_R0,r0);u.reg_write(UC_ARM_REG_R1,r1)
        u.emu_start(addr,RETURN,count=20000)
        assert u.reg_read(UC_ARM_REG_PC)==RETURN
        return u.reg_read(UC_ARM_REG_R0)
    def request():
        setup();state['capture']=True;state['args']=None
        u.reg_write(UC_ARM_REG_R6,HEAP+0x1000);u.reg_write(UC_ARM_REG_R7,HEAP+0x2000)
        u.emu_start(GAME+0x4a9df0,RETURN,count=1000)
        state['capture']=False
        assert state['args'] is not None
        return state['args']
    def consumer(args):
        setup()
        for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,v)
        put(args[1]+0xc,1)
        # Stop at selected branch before config/worker processing, not a mock
        # decision: the original native conditional tests the real argument.
        target=GAME+(0xc5a000 if args[3] else 0xc59df8)
        u.emu_start(GAME+0xc59d78,target,count=1000)
        assert u.reg_read(UC_ARM_REG_PC)==target
        return target
    old=request();assert old[3]==1 and consumer(old)==GAME+0xc5a000
    for guard in (0x4a9df0,0x4a9e04,0x4a9e14,0xc59dc0,0xc59df4,0x4aa154,0x4aa170,0x4abfac,
                  0x3986d4,0x37d488,0x1128de8,0x220bf8,0x52318c,
                  0x1b888c,0x1b8938,0x3806b8,0x43cd08,0x10e45b0,
                  0x3756b8,0x3999b8):
        u.mem_write(GAME,original);put(GAME+guard,0)
        before=bytes(u.mem_read(GAME,size))
        assert call(symbols['tasm2_shop_patch'],GAME,size)==0xffffffff
        assert bytes(u.mem_read(GAME,size))==before
    u.mem_write(GAME,original)
    assert call(symbols['tasm2_shop_patch'],GAME,size)==0
    after=bytes(u.mem_read(GAME,size))
    assert [i for i,(x,y) in enumerate(zip(original,after)) if x!=y]==[0x4a9e04]
    new=request();assert new[:3]==old[:3] and new[3]==0
    assert consumer(new)==GAME+0xc59df8
    # The existing OFFLINE return path sets readiness; the port writes no flag.
    flags=GAME+0x1352d54;u.mem_write(flags,b'\0'*0x28)
    setup();u.emu_start(GAME+0x4aa154,GAME+0x4a9e3c,count=1000)
    assert u.reg_read(UC_ARM_REG_PC)==GAME+0x4a9e3c
    assert word(flags+0x18)==2 and bytes(u.mem_read(flags+2,1))==b'\1'
    # Actual Shop phase 0 must still require both config and OfflineStore ready.
    shop=HEAP+0x3000;store=HEAP+0x4000;items=HEAP+0x5000
    put(shop+4,0);put(shop+0x14,store);put(store,items)
    def gate(ready,config):
        put(shop+4,0);u.mem_write(shop,b'\0')
        u.mem_write(store+8,bytes([ready]));u.mem_write(flags+2,bytes([config]))
        call(GAME+0x4abf58,shop)
        return word(shop+4),bytes(u.mem_read(shop,1))
    assert gate(0,1)==(0,b'\0') and gate(1,0)==(0,b'\0')
    assert gate(1,1)==(1,b'\0') and word(shop+0x18)==items
    print('Shop: original async queue reproduced; checked single-byte change selects native local CRM path; existing offline completion/Shop readiness preserved; changed layout rejected: PASS')
if __name__=='__main__':main()
