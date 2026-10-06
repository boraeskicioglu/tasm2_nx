#!/usr/bin/env python3
"""Actual ARM parser, native costume list and encrypted inventory regression.
Only platform I/O/libc/allocator/locks are mocked; game parser, counters, item
construction, native hash/map lookup, cipher and filtered Suit view execute.
Inventory map topology/unused words are synthetic; native identity, cipher and
UI accessors are real. The crash-address fixture also tests a rebuilt map at a
fresh address with retained decoded quantities. This is not mission restore.
No game data is embedded or exported. Native rendering and save persistence
still require hardware acceptance. Run from the project root after building.
"""
import sys,struct,hashlib,argparse,json,zlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import *
from unicorn.arm_const import *
sys.path.insert(0,str(Path(__file__).resolve().parent))
a=argparse.ArgumentParser(description='Run the linked offline Shop catalog and costume inventory against the original private 1.2.8d ARM library.')
a.add_argument('payload',type=Path);a.add_argument('library',type=Path);a.add_argument('catalog',type=Path,help='Original main OBB or its private 9577-byte catalog member')
a.add_argument('--expect-legacy-name-crash',action='store_true',help='Require the original wrapper to reproduce the invalid +0x14 name read')
a=a.parse_args()
assert hashlib.sha256(a.library.read_bytes()).hexdigest()=='d091fe95c56af681f1a06453e9868622935ecbb759c05c340fc16bf8df2ae62e'
with a.catalog.open('rb') as f:
 if a.catalog.stat().st_size!=9577:f.seek(312677714)
 original_catalog=f.read(9577)
assert len(original_catalog)==9577 and zlib.crc32(original_catalog)==0x036b8ff1
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
parse_calls=[]
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
engine_alloc={GAME+a for a in [0x1a63ac,0x1a618c,0x1a62e0]}
engine_free={GAME+a for a in [0x1a6d2c,0x1a62f0]}
def hook(uc,at,size,data):
 r0=uc.reg_read(UC_ARM_REG_R0);r1=uc.reg_read(UC_ARM_REG_R1);r2=uc.reg_read(UC_ARM_REG_R2)
 name=imports.get(at)
 result=r0
 if at in engine_alloc:result=alloc(r0)
 elif at in engine_free:pass
 elif at in (syms['debugPrintf'],syms['mutexLock'],syms['mutexUnlock']):pass
 elif at==GAME+0x3986d4:
  body=json.loads(string(word(r1)));assert len(body['items'])==19;parse_calls.append(body);return
 elif at==syms['tasm2_catalog_read']:
  raw=original_catalog;result=alloc(len(raw)+1);u.mem_write(result,raw+b'\0');put(r0,len(raw))
 elif at==GAME+0xca18b4:print('native log',string(r1)[:180])
 elif name:
  seen[name]=seen.get(name,0)+1
  if name in ('malloc','_Znwj','_Znaj'):result=alloc(r0)
  elif name=='calloc':result=alloc(r0*r1)
  elif name in ('free','_ZdlPv','_ZdaPv','pthread_mutex_lock','pthread_mutex_unlock','pthread_mutex_init'):result=0
  elif name in ('memset','__aeabi_memset','__aeabi_memset4'):
   if name.startswith('__aeabi'):r1,r2=r2,r1
   uc.mem_write(r0,bytes([r1&255])*r2)
  elif name in ('memcpy','memmove','__aeabi_memcpy','__aeabi_memcpy4','__aeabi_memmove'):uc.mem_write(r0,bytes(uc.mem_read(r1,r2)))
  elif name in ('rand','random','lrand48'):result=12345
  elif name=='strlen':result=len(string(r0))
  elif name in ('strcmp','strncmp','memcmp'):
   x=string(r0) if name!='memcmp' else bytes(uc.mem_read(r0,r2));y=string(r1) if name!='memcmp' else bytes(uc.mem_read(r1,r2))
   if name=='strncmp':x=x[:r2];y=y[:r2]
   result=(x>y)-(x<y)
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
for point in list(imports)+list(engine_alloc)+list(engine_free)+[syms['debugPrintf'],GAME+0xca18b4,GAME+0xcadcb4,GAME+0x389f88,GAME+0x3986d4,syms['tasm2_catalog_read'],syms['mutexLock'],syms['mutexUnlock']]:u.hook_add(UC_HOOK_CODE,hook,begin=point,end=point)
u.hook_add(UC_HOOK_MEM_INVALID,invalid)
def call(at,r0=0,r1=0,r2=0):
 u.reg_write(UC_ARM_REG_SP,STACK+0xf0000);u.reg_write(UC_ARM_REG_LR,RETURN)
 for reg,v in [(UC_ARM_REG_R0,r0),(UC_ARM_REG_R1,r1),(UC_ARM_REG_R2,r2)]:u.reg_write(reg,v)
 try:u.emu_start(at,RETURN,count=30000000)
 except Exception:
  print('FAULT PC/LR/regs',*[hex(u.reg_read(r)) for r in (UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)],flush=True);raise
 if u.reg_read(UC_ARM_REG_PC)!=RETURN:
  print('regs', [hex(u.reg_read(r)) for r in (UC_ARM_REG_R0,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_SP)]);print('json',bytes(u.mem_read(u.reg_read(UC_ARM_REG_R4),0x70)).hex());print(seen)
 assert u.reg_read(UC_ARM_REG_PC)==RETURN,hex(u.reg_read(UC_ARM_REG_PC))
 return u.reg_read(UC_ARM_REG_R0)

names=['Scarlet_Spider_suit','Superior_Spider_suit','Iron_Spider_suit','Symbiote_Spider_suit','Ultimate_Comic_suit','Feature_Spider_suit','EndOfEarth_Spider_suit','Spider_gene']
# A loaded inventory value identifies itself by a native numeric name ID.
# +0x14 is not a usable C string contract: reproduce the exact invalid address
# in all four supplied hardware crashes instead of fabricating a name pointer.
crash_word=0x268db39d
snapshot={name:(8,8) if i==1 else (0,0) for i,name in enumerate(names)}
ids={}
for name in names:
 text=alloc(len(name)+1);u.mem_write(text,name.encode()+b'\0');keybuf=alloc(4);call(GAME+0x220bf8,keybuf,text);ids[name]=word(keybuf)
def tree(nodes,parent):
 if not nodes:return 0
 i=len(nodes)//2;key,node=nodes[i];put(node+4,parent);put(node+8,tree(nodes[:i],node));put(node+12,tree(nodes[i+1:],node));return node
def make_inventory(values,missing=None):
 inventory=alloc(0x50);nodes=[];items={}
 for name in names:
  if name==missing:continue
  node=alloc(0x90);item=node+0x14;key=ids[name];put(node+0x10,key);put(item,key)
  put(item+0x14,crash_word);items[name]=item;nodes.append((key,node))
  for off,initial in zip((4,0x20),values[name]):
   v=alloc(4);put(v,initial);call(GAME+0x1b8938,item+off,v)
 nodes.sort()
 put(inventory+8,tree(nodes,inventory+4));put(inventory+12,nodes[0][1]);put(inventory+16,nodes[-1][1]);put(inventory+20,len(nodes))
 return inventory,items
inventory,native_items=make_inventory(snapshot)
put(GAME+0x134fa14,inventory)
if a.expect_legacy_name_crash:
 call(syms['tasm2_shop_bind'],GAME)
 try:call(syms['tasm2_shop_tick'])
 except (AssertionError,UcError) as ex:
  assert u.reg_read(UC_ARM_REG_R0)==crash_word,str(ex)
  assert str(ex).find('0x268db39d')>=0 or isinstance(ex,UcError),str(ex)
  print('Reproduced legacy boot crash: inventory +0x14 -> strcmp(0x268db39d): PASS')
  sys.exit(0)
 raise AssertionError('Legacy name-pointer crash was not reproduced')

store=call(GAME+0xad5a18)
body=json.loads(string(call(syms['tasm2_shop_catalog_json'])))
assert len(body['items'])==19 and body['items'][:18]==json.loads(original_catalog)['items']
metadata=[]
for it in body['items']:
 price=it['billing_methods'][0]
 metadata.append(dict(type='inapp',price_amount_micros=round(float(price['price'])*1000000),title=it['name'],price=price['display_price'],description=it['description'],productId=it['entry_id'],price_currency_code=price['currency']))
raw=json.dumps(dict(store_items=metadata)).encode();u.mem_write(HEAP+0x1000,raw+b'\0');call(GAME+0x1128de8,HEAP,HEAP+0x1000,0)
call(GAME+0xae0df4,store,HEAP);assert word(store+0x54)==19
manager=alloc(0x150);request=alloc(0x200);put(manager+0x44,request)
for offset in (0x14,0x2c):
 put(manager+offset+12,manager+offset+4);put(manager+offset+16,manager+offset+4)
# Fifty preexisting descriptor objects, using the native ctor/string ABI.
desc=[]
for i in range(50):
 name=('fixture_%02d'%i).encode();text=alloc(len(name)+1);u.mem_write(text,name+b'\0');ss=alloc(4);call(GAME+0x1128de8,ss,text,0)
 node=alloc(24);put(node+0x10,word(ss));item=alloc(0x144);call(GAME+0x383640,item);put(node+0x14,item);desc.append((name,node))
put(manager+0x34,tree(desc,manager+0x30));put(manager+0x38,desc[0][1]);put(manager+0x3c,desc[-1][1]);put(manager+0x40,50)
cat=GAME+0x1350314;put(cat+12,cat+4);put(cat+16,cat+4);put(GAME+0x134f728,manager)
call(syms['tasm2_shop_bind'],GAME)
put(store+0x54,0);call(syms['tasm2_shop_tick']);assert not parse_calls and word(manager+0x48)==0
put(store+0x54,19);call(syms['tasm2_shop_tick']);assert len(parse_calls)==1
assert word(manager+0x48)==5
assert call(GAME+0x37d488,manager,1)==7
assert call(GAME+0x37d488,manager,2)==2
vector=call(GAME+0x3756b8,manager,2)
assert (word(vector+4)-word(vector))//4==7
# Check actual Suit view identity, not only its length or JSON category count.
view=[]
for slot in range(word(vector),word(vector+4),4):
 item=word(slot);native_string=call(GAME+0x205014,item+0x10)
 view.append(string(word(native_string)).decode())
assert set(view)==set(names[:7]) and len(view)==len(set(view))
# The UI's original owned-quantity accessor must read each granted profile.
temp=alloc(4);owned=alloc(0x20)
for index,name in enumerate(names):
 call(GAME+0x52bc24,owned,native_items[name])
 assert word(call(GAME+0x1b888c,owned,temp))==(8 if index==1 else 0 if index==7 else 1)

 for off in (4,0x20):
  value=word(call(GAME+0x1b888c,native_items[name]+off,temp));expected=8 if index==1 else 0 if index==7 else 1
  assert value==expected,(name,off,value,expected)
for i in range(600):call(syms['tasm2_shop_tick'])
assert len(parse_calls)==1
vector=call(GAME+0x3756b8,manager,2)
assert (word(vector+4)-word(vector))//4==7
print('Shop: linked production catalog conversion, real native parser/counters, seven active suit rows, existing native state-5 continuation: PASS')


# Serialize only synthetic decoded inventory quantities, rebuild at fresh
# addresses and rerun the linked production hook without a pointer at +0x14.
# Native UserData file codecs have their separate checkpoint regression.
temp=alloc(4)
saved={name:tuple(word(call(GAME+0x1b888c,item+off,temp)) for off in (4,0x20)) for name,item in native_items.items()}
saved=json.loads(json.dumps(saved))
old_items=native_items
inventory,native_items=make_inventory(saved)
assert all(native_items[name]!=old_items[name] for name in names)
put(GAME+0x134fa14,inventory)
for i in range(600):call(syms['tasm2_shop_tick'])
assert len(parse_calls)==1
for name,item in native_items.items():
 assert word(item)==ids[name] and word(item+0x14)==crash_word
 native_name=call(GAME+0x205014,item)
 assert string(word(native_name)).decode()==name
 for offset,accessor in ((4,0x52bc24),(0x20,0x52be68)):
  call(GAME+accessor,owned,item)
  expected=saved[name][0 if offset==4 else 1]
  assert word(call(GAME+0x1b888c,owned,temp))==expected
  assert word(call(GAME+0x1b888c,item+offset,temp))==expected
# A partially constructed map grants nothing, even to earlier valid entries.
inventory,partial=make_inventory(snapshot,missing=names[6])
put(GAME+0x134fa14,inventory)
for i in range(60):call(syms['tasm2_shop_tick'])
for name,item in partial.items():
 for offset,expected in zip((4,0x20),snapshot[name]):
  assert word(call(GAME+0x1b888c,item+offset,temp))==expected
put(GAME+0x134fa14,0)
for i in range(60):call(syms['tasm2_shop_tick'])
assert len(parse_calls)==1
print('Reload regression: invalid name word ignored, fresh map, native ID/owned/available UI, positive quantities and noncostume retained, absent/partial inventory: PASS')
