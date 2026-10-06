#!/usr/bin/env python3
"""Execute linked local-save patch and original 1.2.8d UserData codecs.
Platform file I/O, memory, locks, clock formatting and config name lookup are
mocked. Constructors, serialization, compression, native JSON writer, dual
file encoding, read/decode/application and pending checkpoint flush are real
ARM code. Synthetic fields only: no game data or player saves are packaged.
HOME focus delivery and complete mission resume still require Switch testing.
"""
import sys,struct,hashlib,argparse,json,zlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import *
from unicorn.arm_const import *
sys.path.insert(0,str(Path('tests').resolve()))
a=argparse.ArgumentParser(description=__doc__)
a.add_argument('payload',type=Path);a.add_argument('library',type=Path);
a=a.parse_args()
assert hashlib.sha256(a.library.read_bytes()).hexdigest()=='d091fe95c56af681f1a06453e9868622935ecbb759c05c340fc16bf8df2ae62e'
from test_space_native import load, WRAPPER,GAME,HEAP,STACK,RETURN,LIBRARY_SHA256
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);syms=load(u,a.payload,WRAPPER);gsyms=load(u,a.library,GAME)
u.mem_map(HEAP,0x2000000);u.mem_map(STACK,0x100000);u.mem_map(RETURN,0x20000)
u.reg_write(UC_ARM_REG_C1_C0_2,0xf<<20);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
def put(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
imports={};external=RETURN+0x1000
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
    else:
     imports[external]=sym.name;put(at,external);external+=4
for name in ('malloc','calloc','free','strlen','strcmp','strncmp','memcmp','memcpy','memmove','memset'):
 if name in syms:imports[syms[name]]=name
arena=HEAP+0x10000
seen={}
def alloc(n):
 global arena
 at=arena;arena+=(n+15)&~15
 assert arena<HEAP+0x2000000
 return at
def string(at):
 out=b''
 for i in range(0,0x10000,256):
  chunk=bytes(u.mem_read(at+i,256));end=chunk.find(b'\0')
  if end>=0:return out+chunk[:end]
  out+=chunk
 raise AssertionError('unterminated')
engine_alloc={GAME+a for a in [0x1a63ac,0x1a618c,0x1a62e0,0x1a6bf0]}
engine_free={GAME+a for a in [0x1a6d2c,0x1a62f0]}
def libc_hook(uc,at,size,data):
 r0=uc.reg_read(UC_ARM_REG_R0);r1=uc.reg_read(UC_ARM_REG_R1);r2=uc.reg_read(UC_ARM_REG_R2)
 name=imports.get(at)
 result=r0
 if at in engine_alloc:result=alloc(r0)
 elif at in engine_free:pass
 elif at in (syms['debugPrintf'],syms['mutexLock'],syms['mutexUnlock']):pass
 elif at==GAME+0xca18b4:print('native log',string(r1)[:180])
 elif name:
  seen[name]=seen.get(name,0)+1
  if name in ('malloc','_Znwj','_Znaj'):result=alloc(r0)
  elif name=='calloc':result=alloc(r0*r1)
  elif name=='realloc':
   result=alloc(r1)
   if r0:uc.mem_write(result,bytes(uc.mem_read(r0,r1)))
  elif name in ('free','_ZdlPv','_ZdaPv','pthread_mutex_lock','pthread_mutex_unlock','pthread_mutex_init'):result=0
  elif name in ('memset','__aeabi_memset','__aeabi_memset4'):
   if name.startswith('__aeabi'):r1,r2=r2,r1
   uc.mem_write(r0,bytes([r1&255])*r2)
  elif name in ('memcpy','memmove','__aeabi_memcpy','__aeabi_memcpy4','__aeabi_memmove'):uc.mem_write(r0,bytes(uc.mem_read(r1,r2)))
  elif name in ('clock','time','rand','random','lrand48'):result=12345
  elif name=='strlen':result=len(string(r0))
  elif name in ('strcmp','strncmp','memcmp'):
   x=string(r0) if name!='memcmp' else bytes(uc.mem_read(r0,r2));y=string(r1) if name!='memcmp' else bytes(uc.mem_read(r1,r2))
   if name=='strncmp':x=x[:r2];y=y[:r2]
   result=(x>y)-(x<y)
  elif name=='gmtime':result=clock_value
  elif name=='sscanf':
   fmt=string(r1).decode();text=string(r0).decode()
   import re
   m=re.match(r'[+-]?(?:[0-9]+(?:[.][0-9]*)?|[.][0-9]+)(?:[eE][+-]?[0-9]+)?',text)
   assert fmt in ('%lf','%f','%d','%u'),fmt
   result=bool(m)
   if m:uc.mem_write(r2,struct.pack('<d' if fmt=='%lf' else '<f' if fmt=='%f' else '<i',float(m[0]) if 'f' in fmt else int(m[0])))
  elif name=='strchr':
   x=string(r0)+b'\0';j=x.find(bytes([r1&255]));result=r0+j if j>=0 else 0
  elif name=='sprintf':
   fmt=string(r1).decode();r3=uc.reg_read(UC_ARM_REG_R3)
   if 'f' in fmt or 'g' in fmt:out=(fmt%struct.unpack('<d',struct.pack('<II',r2,r3))[0]).encode()
   elif fmt.count('%s')==2:out=(fmt%(string(r2).decode(),string(r3).decode())).encode()
   elif '%s' in fmt:out=(fmt%string(r2).decode()).encode()
   else:out=(fmt%r2).encode()
   uc.mem_write(r0,out+b'\0');result=len(out)
  elif name=='strpbrk':
   x=string(r0);chars=string(r1);j=next((i for i,c in enumerate(x) if c in chars),-1);result=r0+j if j>=0 else 0
  elif name=='__cxa_guard_acquire':result=not word(r0)
  elif name=='__cxa_guard_release':put(r0,1);result=0
  elif name in ('__cxa_atexit','__aeabi_atexit'):result=0
  else:raise AssertionError(('Unhandled import',name,hex(uc.reg_read(UC_ARM_REG_LR)),r0,r1,r2))
 else:return
 uc.reg_write(UC_ARM_REG_R0,result&0xffffffff);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
def invalid(uc,access,address,size,value,data):raise AssertionError(('unmapped',hex(address),hex(uc.reg_read(UC_ARM_REG_PC))))

import tempfile,os
import tempfile
temporary=tempfile.TemporaryDirectory(prefix='tasm2-userdata-')
root=Path(temporary.name);fds={};nextfd=0x60000000
calls=[]
metadata_root=alloc(100);u.mem_write(metadata_root,b'/data/data/com.gameloft.android.ANMP.GloftASHM/files\0')
metadata_names={}
for i in range(20):
 at=alloc(24);u.mem_write(at,('SaveIndex' if i==16 else 'Fixture_%d'%i).encode()+b'\0');metadata_names[i]=at
clock_value=alloc(44)
def local(p):
 path=string(p).decode();prefix='/data/data/com.gameloft.android.ANMP.GloftASHM/files/'
 assert path.startswith(prefix),path
 return root/path[len(prefix):]
def hook(uc,at,size,data):
 global nextfd
 name=imports.get(at);r0=uc.reg_read(UC_ARM_REG_R0);r1=uc.reg_read(UC_ARM_REG_R1);r2=uc.reg_read(UC_ARM_REG_R2);r3=uc.reg_read(UC_ARM_REG_R3)
 if name in ('fopen','fclose','fwrite','fread','fseek','ftell','fflush','rename','unlink','remove','access'):
  result=0
  if name=='fopen':
   p=local(r0);mode=string(r1).decode();calls.append((name,p.name,mode))
   try:fds[nextfd]=p.open(mode if 'b' in mode else mode+'b');result=nextfd;nextfd+=4
   except OSError:result=0
  elif name=='fclose':fds.pop(r0).close()
  elif name=='fflush':fds[r0].flush()
  elif name=='fwrite':result=fds[r3].write(bytes(uc.mem_read(r0,r1*r2)))//r1
  elif name=='fread':
   raw=fds[r3].read(r1*r2);uc.mem_write(r0,raw);result=len(raw)//r1
  elif name=='fseek':fds[r0].seek(r1 if r1<0x80000000 else r1-0x100000000,r2)
  elif name=='ftell':result=fds[r0].tell()
  elif name=='rename':
   src,dst=local(r0),local(r1);calls.append((name,str(src),str(dst)));print(calls[-1],flush=True)
   try:src.replace(dst)
   except OSError:result=-1
  elif name in ('unlink','remove'):
   p=local(r0);calls.append((name,str(p)));print(calls[-1],flush=True)
   try:p.unlink()
   except OSError:result=-1
  elif name=='access':result=0 if local(r0).exists() else -1
  uc.reg_write(UC_ARM_REG_R0,result&0xffffffff);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
 if at==GAME+0x3cec34:
  put(r1,word(clock_text));uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
 if at in (GAME+0x3c6604,GAME+0x547004,GAME+0x3c66fc):
  result=metadata_root if at==GAME+0x3c6604 else metadata_names[word(r0+0x10)] if at==GAME+0x547004 else clock_value
  uc.reg_write(UC_ARM_REG_R0,result);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
 if at==GAME+0xca18b4:uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
 libc_hook(uc,at,size,data)
for point in list(imports)+list(engine_alloc)+list(engine_free)+[syms['debugPrintf'],GAME+0xca18b4,GAME+0x3c6604,GAME+0x547004,GAME+0x3c66fc,GAME+0x3cec34,syms['mutexLock'],syms['mutexUnlock']]:u.hook_add(UC_HOOK_CODE,hook,begin=point,end=point)
u.hook_add(UC_HOOK_MEM_INVALID,invalid)
def call(at,r0=0,r1=0,r2=0,r3=0,stop=RETURN):
 u.reg_write(UC_ARM_REG_SP,STACK+0xf0000);u.reg_write(UC_ARM_REG_LR,RETURN)
 put(STACK+0xf0000,0)
 for reg,v in [(UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),(UC_ARM_REG_R2,r2),(UC_ARM_REG_R3,r3)]:u.reg_write(reg,v)
 try:u.emu_start(at,stop,count=30000000)
 except Exception:
  print('FAULT PC/LR/regs',*[hex(u.reg_read(r)) for r in (UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)],flush=True);raise
 assert u.reg_read(UC_ARM_REG_PC)==stop,hex(u.reg_read(UC_ARM_REG_PC))
 return u.reg_read(UC_ARM_REG_R0)
def cow(s):
 text=alloc(len(s)+1);u.mem_write(text,s.encode()+b'\0');out=alloc(4);call(GAME+0x1128de8,out,text);return out
# android32 calls init arrays, including construction of empty COW storage.



clock_text=cow('Mon, 05 Oct 2026 14:00:00')
image_size=next(end-begin+1 for begin,end,_ in u.mem_regions() if begin==GAME)
original=bytes(u.mem_read(GAME,image_size))
def profile_new():
 p=alloc(0xc00);put(GAME+0x134fa08,p)
 for i in range(20):put(p+0x8d8+20*i+0x10,word(cow('')))
 return p
profile=profile_new()
def object_new(ctor=0x51ebf0):
 obj=alloc(0x200);call(GAME+ctor,obj);return obj

def serialized(obj,flag,number):
 u.mem_write(obj+0x50,bytes([flag]));put(obj+0x54,number)
 u.mem_write(obj+0x1c,b'\0\1')
 call(word(word(obj)+12),obj)
 return profile+0x8d8+20*word(obj+0x10)

# Reproduce why settings saved and a cloud-default block did not.
progress=object_new();settings=object_new(0x51ec30)
assert bytes(u.mem_read(progress+0x19,1))==b'\0'
assert bytes(u.mem_read(settings+0x19,1))==b'\1'
block=serialized(progress,1,0x12345678)
call(GAME+0x547f20,progress,stop=GAME+0x548280)
assert not any(mode=='wb+' for _,_,mode in calls)
# SaveIndex's original existing-file/cloud-ready gate also prevents first save.
index=object_new();put(index+0x10,16)
call(GAME+0x547350,index,cow('{"synthetic":1}'))
assert not any(mode=='wb+' for _,_,mode in calls)

# Reject changed binary layouts atomically, including the local constructor.
original=bytes(u.mem_read(GAME,image_size))
guards=[0x51e8a0,0x51e8dc,0x51e8ec,0x51e8f4,0x547370,0x54737c,
        0x547384,0x547568,0x547580,0x547f20,0x5488a8,0x5475f0,0x548ca8]
for at in guards:
 before_word=word(GAME+at);put(GAME+at,before_word^1)
 before=bytes(u.mem_read(GAME,image_size))
 assert call(syms['tasm2_checkpoint_patch'],GAME,image_size)==0xffffffff
 assert bytes(u.mem_read(GAME,image_size))==before
 put(GAME+at,before_word)
for ptr,size in [(0,image_size),(GAME,0x134fa0b)]:
 assert call(syms['tasm2_checkpoint_patch'],ptr,size)==0xffffffff
 assert bytes(u.mem_read(GAME,image_size))==original
assert call(syms['tasm2_checkpoint_patch'],GAME,image_size)==0
patched=bytes(u.mem_read(GAME,image_size))
assert all((0x51e8ec<=i<0x51e8f8 or 0x547384<=i<0x547388)
           for i,(x,y) in enumerate(zip(original,patched)) if x!=y)
# Native flags and JSON initialization remain intact (including old settings).
for ctor in (0x51ebf0,0x51ec30,0x51ec98,0x51ef48,0x51ef8c,0x51f184,0x51f1cc,0x51f218):
 obj=object_new(ctor)
 assert bytes(u.mem_read(obj+0x19,1))==b'\1'
 assert bytes(u.mem_read(obj+0x1a,5))==b'\0'*5

progress=object_new();block=serialized(progress,1,0x12345678)
assert word(block)==0 # normal: serialized COW base64 is at +10, not +0
assert word(block+4)==5 and string(word(block+16))
u.mem_write(profile+0xb00,b'\1');put(profile+0x830+4*13,progress)
call(syms['tasm2_checkpoint_bind'],GAME)
assert call(syms['tasm2_checkpoint_flush'])==1
assert bytes(u.mem_read(block+12,1))==b'\0'
assert call(syms['tasm2_checkpoint_flush'])==0
path=root/'ud_Fixture_13.sav';raw=path.read_bytes()
length=struct.unpack('<I',raw[:4])[0]^42
assert len(raw)==4+2*length
first=bytes(c^((i+42)%127) for i,c in enumerate(raw[4:4+length]))
second=bytes(c^((7*i)%23) for i,c in enumerate(raw[4+length:]))
assert second==raw[4:4+length]
body=json.loads(first);assert body and any(isinstance(v,str) and len(v)>20 for v in body.values())
# Fresh native profile, real local loader, base64/crypto/inflate, native apply.
profile=profile_new();new=object_new()
call(GAME+0x548bf4,new)
assert bytes(u.mem_read(new+0x50,1))==b'\1' and word(new+0x54)==0x12345678
assert not fds

# An update survives another fresh-profile reload, not just first creation.
block=serialized(new,0,0x2468ace0)
u.mem_write(profile+0xb00,b'\1')
call(GAME+0x54882c,new,0) # real native Update publishes the ordinary autosave
assert bytes(u.mem_read(block+12,1))==b'\0'
profile=profile_new();again=object_new();call(GAME+0x548bf4,again)
assert bytes(u.mem_read(again+0x50,1))==b'\0' and word(again+0x54)==0x2468ace0

# First offline SaveIndex succeeds for every preexisting/cloud flag combination.
index=object_new();put(index+0x10,16)
for existing in (0,1):
 for cloud in (0,1):
  u.mem_write(profile+0xb50,bytes([existing]));u.mem_write(profile+0xb52,bytes([cloud]))
  call(GAME+0x547350,index,cow('{"synthetic":%d}'%(existing*2+cloud)))
  assert (root/'ud_SaveIndex.sav').exists()
  assert bytes(u.mem_read(profile+0xb52,1))==bytes([cloud])

# Lifecycle flush must not touch partial/unready/resetting/clean profile blocks.
put(profile+0x830+4*13,again);block=serialized(again,1,99)
for off,flag in [(0xb00,0),(0xb3c,1),(0xb3d,1)]:
 u.mem_write(profile+0xb00,b'\1');u.mem_write(profile+0xb3c,b'\0\0')
 u.mem_write(profile+off,bytes([flag]));assert call(syms['tasm2_checkpoint_flush'])==0
u.mem_write(profile+0xb00,b'\1');u.mem_write(profile+0xb3c,b'\0\0')
for off,flag in [(0x19,0),(0x1c,1),(0x1d,0)]:
 original_flag=bytes(u.mem_read(again+off,1));u.mem_write(again+off,bytes([flag]))
 assert call(syms['tasm2_checkpoint_flush'])==0
 u.mem_write(again+off,original_flag)
assert call(syms['tasm2_checkpoint_flush'])==1
assert not fds
temporary.cleanup()
print('Checkpoint: old cloud-only omission and first SaveIndex gate reproduced; linked checked patch; actual native serialization/JSON/dual encoding/local reload/apply/Update autosave; updated values and pending-only flush: PASS')
