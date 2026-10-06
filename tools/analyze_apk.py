"""Inspect an owner's APK; writes private analysis only, never release assets."""
import argparse, hashlib, json, re, zipfile
from pathlib import Path
from loguru import logger
logger.remove()
from androguard.core.apk import APK
from androguard.core.dex import DEX
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
p=argparse.ArgumentParser(); p.add_argument('apk'); p.add_argument('--out',default='.analysis'); a=p.parse_args()
out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
apk=APK(a.apk)
meta={'package':apk.get_package(),'version_name':apk.get_androidversion_name(),'version_code':apk.get_androidversion_code(),'apk_sha256':hashlib.sha256(Path(a.apk).read_bytes()).hexdigest()}
print(json.dumps(meta,indent=2))
z=zipfile.ZipFile(a.apk)
libs=out/'libs'; libs.mkdir(exist_ok=True)
report={}
for item in z.namelist():
 if not item.startswith('lib/armeabi-v7a/') or not item.endswith('.so'): continue
 name=Path(item).name; path=libs/name; path.write_bytes(z.read(item))
 with path.open('rb') as f:
  e=ELFFile(f); syms=list(e.get_section_by_name('.dynsym').iter_symbols()); dyn=e.get_section_by_name('.dynamic')
  deps=[t.needed for t in dyn.iter_tags() if t.entry.d_tag=='DT_NEEDED']
  exports={s.name:{'value':s['st_value'],'size':s['st_size'],'type':s['st_info']['type']} for s in syms if s.name and s['st_shndx']!='SHN_UNDEF'}
  imports=[s.name for s in syms if s.name and s['st_shndx']=='SHN_UNDEF']
  report[name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'class':e.elfclass,'machine':e['e_machine'],'needed':deps,'imports':imports,'exports':exports,'loads':[dict(s.header) for s in e.iter_segments() if s['p_type']=='PT_LOAD']}
  print(name,'needed',deps,'imports',len(imports),'exports',len(exports),'mapped',hex(max(s['p_vaddr']+s['p_memsz'] for s in e.iter_segments() if s['p_type']=='PT_LOAD')))
  jni=[n for n in exports if n.startswith('Java_') or n=='JNI_OnLoad' or 'ControllerConnected' in n or 'ControllerDisconnected' in n or 'HandleInputEvents' in n]
  print('\n'.join(n+' '+hex(exports[n]['value']) for n in jni))
  with (out/(name+'.asm.txt')).open('w',encoding='utf-8') as af:
   for n in jni:
    s=exports[n]; addr=s['value']&~1; seg=next((t for t in e.iter_segments() if t['p_type']=='PT_LOAD' and t['p_vaddr']<=addr<t['p_vaddr']+t['p_filesz']),None)
    if not seg: continue
    f.seek(seg['p_offset']+addr-seg['p_vaddr']); code=f.read(min(s['size'],4096)); af.write('\n'+n+'\n')
    cs=Cs(CS_ARCH_ARM,CS_MODE_THUMB if s['value']&1 else CS_MODE_ARM)
    for ins in cs.disasm(code,addr): af.write(f'{ins.address:08x} {ins.bytes.hex():10} {ins.mnemonic:10} {ins.op_str}\n')
  strs=re.findall(rb'[ -~]{5,}',path.read_bytes())
  (out/(name+'.strings.txt')).write_text('\n'.join(s.decode('ascii') for s in strs),encoding='utf-8')
(out/'elf.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
(out/'apk.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
with (out/'dex.txt').open('w',encoding='utf-8') as df, (out/'native_methods.txt').open('w',encoding='utf-8') as nf:
 for entry in z.namelist():
  if not re.fullmatch(r'classes\d*\.dex',entry): continue
  d=DEX(z.read(entry))
  for c in d.get_classes():
   for m in c.get_methods():
    ident=c.get_name()+'->'+m.get_name()+m.get_descriptor()
    if 'native' in m.get_access_flags_string(): nf.write(ident+'\n')
    if not (c.get_name().startswith('Lcom/gameloft/') and re.search(r'GL2JNI|GLUtils/Device|GLUtils/SUtils|/Game;|/Game\$|/Device;|/HID|/DataSharing|/SendInfo|/Gamepad|/Controller|/MainActivity',c.get_name())): continue
    df.write('\n'+ident+' '+m.get_access_flags_string()+'\n')
    code=m.get_code()
    if code:
     df.write('registers '+str(code.get_registers_size())+' params '+str(code.get_ins_size())+'\n')
     off=0
     for ins in m.get_instructions():
      df.write(f'{off:04x}: {ins.get_name()} {ins.get_output()}\n'); off+=ins.get_length()
print('Private reports written to',out)