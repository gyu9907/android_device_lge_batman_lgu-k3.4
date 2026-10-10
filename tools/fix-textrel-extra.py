#!/usr/bin/env python3
"""Exact-hash PIC startup and JPEG fixups, after stock extraction validation."""
import argparse, importlib.util, json, os, struct, tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('crt_fix',Path(__file__).with_name('fix-textrel.py'))
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
require,Elf,sha=f.require,f.Elf,f.sha

def branch(src,dst,link=False):
    delta=dst-src-8
    require(delta%4==0 and -(1<<25)<=delta<(1<<25),'ARM branch out of range')
    return (0xeb000000 if link else 0xea000000)|((delta//4)&0xffffff)

def finish_rels(out,before,rels,shoff=None):
    count=before.d.get(0x6ffffffa,0)
    require(count<=len(before.rels) and all(i==23 for _,i in before.rels[:count]),'Invalid RELCOUNT')
    count=0
    for _,info in rels:
        if info!=23:break
        count+=1
    size=len(rels)*8
    require(size<=before.d[18],'Relocation growth')
    out[before.rel_offset:before.rel_offset+before.d[18]]=b''.join(struct.pack('<2I',*r) for r in rels)+bytes(before.d[18]-size)
    dyn=[]
    for tag,val in before.dynamic:
        if tag==22:continue
        if tag==18:val=size
        elif tag==30:val &= ~4
        elif tag==0x6ffffffa:val=count
        dyn.append((tag,val))
    packed=b''.join(struct.pack('<2I',*d) for d in dyn)
    require(len(packed)+8<=before.dp[4],'Dynamic table overflow')
    out[before.dp[1]:before.dp[1]+before.dp[4]]=packed+bytes(before.dp[4]-len(packed))
    sections=[i for i,s in enumerate(before.sh) if s[1]==9 and s[4]==before.rel_offset and s[5]==before.d[18]]
    require(len(sections)==1,'Missing REL section')
    sizeoff=(before.e[6] if shoff is None else shoff)+sections[0]*40+20
    struct.pack_into('<I',out,sizeoff,size)
    after=Elf(out)
    require(not after.nonw() and 22 not in after.d and not after.d.get(30,0)&4,'Remaining TEXTREL')
    require(after.rels==rels and before.plt_rels==after.plt_rels and before.symbols==after.symbols and before.strings==after.strings,'Unexpected ABI/relocations')
    return after

def startup(data,rule):
    b=Elf(data);entry=b.e[4]
    require(entry%4==0 and [b.word(entry+i*4) for i in range(8)]==[0xe1a0000d,0xe3a01000,0xe59f2020,0xe28f300c,0xe59f401c,0xe12fff34,0xe3a00000,0xe12fff10],'Unexpected startup')
    nonw=b.nonw();require(len(nonw)==6 and {a for a,_ in nonw}==set(range(entry+32,entry+56,4)),'Unexpected startup relocations')
    require((entry+48,23) in nonw,'Main not RELATIVE')
    libc=[(a,i) for a,i in nonw if a==entry+52]
    require(len(libc)==1 and libc[0][1]&255==2 and b.symbols[libc[0][1]>>8][0]=='__libc_init' and b.word(entry+52)==0,'Unexpected libc init binding')
    slots=[a for a,i in b.plt_rels if i&255==22 and b.symbols[i>>8][0]=='__libc_init']
    require(len(slots)==1,'Expected existing libc init PLT')
    plts=[]
    for s in b.sh:
        if s[2]&4:
            for addr in range(s[3],s[3]+s[5]-11,4):
                if b.word(addr)&0xfffff000==0xe28fc000:
                    try:
                        if f.plt_slot(b,addr)==slots[0]:plts.append(addr)
                    except ValueError:pass
    require(len(plts)==1,'Unproven libc PLT')
    require(len(b.loads)==2 and [p[6] for p in b.loads]==[5,6],'Unsupported LOAD layout')
    rw=b.loads[-1];insert=rw[1]+rw[4];desc=(rw[2]+rw[5]+3)&~3
    newsize=desc+16-rw[2];growth=newsize-rw[4]
    require(growth>0 and growth%4==0 and b.e[6]>=insert,'Invalid insertion')
    require(all(p==rw or p[4]==0 or p[1]+p[4]<=insert for p in b.ph),'PHDR overlaps insertion')
    for s in b.sh:
        if s[1]!=8 and s[5]:
            require(s[4]+s[5]<=insert or (s[4]>=insert and not s[2]&2),'Section overlaps insertion')
            if s[4]>=insert:require(s[8]<=1 or growth%s[8]==0,'Shift breaks section alignment')
    out=bytearray(data[:insert]+bytes(growth)+data[insert:])
    descriptor_off=rw[1]+desc-rw[2]
    out[descriptor_off:descriptor_off+16]=data[b.off(entry+32,16):b.off(entry+32,16)+16]
    shoff=b.e[6]+growth;struct.pack_into('<I',out,32,shoff)
    pi=b.ph.index(rw);struct.pack_into('<2I',out,b.e[5]+pi*32+16,newsize,newsize)
    for i,s in enumerate(b.sh):
        if s[1]!=8 and s[4]>=insert:struct.pack_into('<I',out,shoff+i*40+16,s[4]+growth)
    # Move only the exact ARM data mapping symbol. It is local .symtab metadata.
    for s in b.sh:
        if s[1]!=2:continue
        require(s[9]==16,'Unexpected symtab')
        st=b.sh[s[6]];strings=data[st[4]:st[4]+st[5]]
        for off in range(s[4],s[4]+s[5],16):
            name,val,size,info,other,ndx=struct.unpack_from('<IIIBBH',data,off)
            name=strings[name:strings.find(b'\0',name)]
            if entry+32<=val<entry+56:
                require(name==b'$d' and val==entry+32 and size==0 and info==0,'External symbol points into startup literals')
                struct.pack_into('<I',out,off+(growth if off>=insert else 0)+4,entry+36)
    code=[0xe1a0000d,0xe3a01000,0xe59f2014,0xe08f2002,0xe59f3010,0xe08f3003,branch(entry+24,plts[0],True),0xe3a00000,0xe12fff10,(b.word(entry+48)-(entry+20))&0xffffffff,(desc-(entry+28))&0xffffffff,0,0,0]
    struct.pack_into('<14I',out,b.off(entry,56),*code)
    rels=[]
    for a,i in b.rels:
        if a in (entry+48,entry+52):continue
        if entry+32<=a<entry+48:a=desc+a-(entry+32)
        rels.append((a,i))
    a=finish_rels(out,b,rels,shoff)
    require(a.e[4]==entry and a.loads[-1][6]==6 and a.loads[0]==b.loads[0],'Changed entry/RX')
    require(out[insert:descriptor_off]==bytes(descriptor_off-insert),'Original BSS not zero')
    # All alloc sections except startup, REL and DYNAMIC retain their contents;
    # startup shares .text, so compare remaining bytes of that section as well.
    changed=[(b.off(entry),b.off(entry)+56),(b.rel_offset,b.rel_offset+b.d[18]),(b.dp[1],b.dp[1]+b.dp[4]),(0,52),(b.e[5]+pi*32,b.e[5]+(pi+1)*32)]
    require(all(x==y or any(lo<=i<hi for lo,hi in changed) for i,(x,y) in enumerate(zip(data[:insert],out[:insert]))),'Unexpected mapped byte change')
    return bytes(out)

def jpeg(data,rule):
    b=Elf(data);crt=rule['literal'];start=crt-12;sites=rule['consumers']
    require(set(b.nonw())=={(crt,23)}|{(s['literal'],23) for s in sites} and len(b.nonw())==11,'Unexpected JPEG relocations')
    require([b.word(start),b.word(start+4)]==[0xe28f0004,0xe5900000],'Unexpected JPEG CRT')
    require(b.word(crt)==next(s[1] for s in b.symbols if s[0]=='__dso_handle'),'Unexpected DSO')
    slot=f.plt_slot(b,f.arm_branch(b,start+8))
    require(any(a==slot and i&255==22 and b.symbols[i>>8][0]=='__cxa_finalize' for a,i in b.plt_rels),'Unproven fini call')
    require(any(b.word(b.d[26]+i)==start and (b.d[26]+i,23) in b.rels for i in range(0,b.d[28],4)),'Unproven fini hook')
    rx=b.loads[0];end=rx[2]+rx[5];off=rx[1]+rx[4];size=len(sites)*16
    require(rx[6]==5 and rx[4]==rx[5] and end==rule['stub_start'],'Unexpected JPEG RX end')
    require(off+size<=b.loads[1][1] and end+size<=b.loads[1][2] and data[off:off+size]==bytes(size),'No empty RX extension gap')
    require(all(s[1]==8 or s[5]==0 or s[4]+s[5]<=off or s[4]>=off+size for s in b.sh),'Stub overlaps section')
    out=bytearray(data)
    struct.pack_into('<2I',out,b.off(start),0xe59f0004,0xe08f0000)
    struct.pack_into('<I',out,b.off(crt),(b.word(crt)-crt)&0xffffffff)
    for n,s in enumerate(sites):
        ins,reg,lit,target=s['instruction'],s['register'],s['literal'],s['target'];stub=end+n*16
        require(0<=reg<=12 and b.word(ins)==(0xe59f0000|(reg<<12)|(lit-ins-8)) and b.word(lit)==target,'Unexpected JPEG table consumer')
        struct.pack_into('<I',out,b.off(ins),branch(ins,stub))
        struct.pack_into('<4I',out,off+n*16,0xe59f0004|(reg<<12),0xe08f0000|(reg<<12)|reg,branch(stub+8,ins+4),(target-stub-12)&0xffffffff)
        struct.pack_into('<I',out,b.off(lit),0) # no absolute table pointer remains
    pi=b.ph.index(rx);struct.pack_into('<2I',out,b.e[5]+pi*32+16,rx[4]+size,rx[5]+size)
    removed=set(b.nonw());a=finish_rels(out,b,[r for r in b.rels if r not in removed])
    require(a.loads[1:]==b.loads[1:] and a.e==b.e,'Unexpected JPEG layout change')
    allowed=[(b.off(start),b.off(start)+16),(off,off+size),(b.e[5]+pi*32+16,b.e[5]+pi*32+24),(b.rel_offset,b.rel_offset+b.d[18]),(b.dp[1],b.dp[1]+b.dp[4])]
    allowed += [(b.off(s[k]),b.off(s[k])+4) for s in sites for k in ('instruction','literal')]
    allowed += [(b.e[6]+i*40+20,b.e[6]+i*40+24) for i,s in enumerate(b.sh) if s[1]==9 and s[4]==b.rel_offset]
    require(len(out)==len(data) and all(x==y or any(lo<=i<hi for lo,hi in allowed) for i,(x,y) in enumerate(zip(data,out))),'Unexpected JPEG byte changes')
    return bytes(out)

def transform(data,rule):
    require(sha(data)==rule['input_sha256'],'Unsupported pre-fixup hash')
    require(rule['kind'] in ('startup','jpeg'),'Unknown kind')
    return startup(data,rule) if rule['kind']=='startup' else jpeg(data,rule)

def main():
    p=argparse.ArgumentParser(description=__doc__);m=p.add_mutually_exclusive_group(required=True)
    m.add_argument('--check',action='store_true');m.add_argument('--apply',action='store_true');m.add_argument('--verify',action='store_true');p.add_argument('root',type=Path);args=p.parse_args()
    manifest=json.loads(Path(__file__).with_name('textrel-extra-fixups.json').read_text());rules=manifest['files']
    require(manifest['version']==1 and len(rules)==18 and len({r['path'] for r in rules})==18,'Invalid extra manifest')
    root=args.root.resolve(strict=True);pending=[]
    for rule in rules:
        rel=Path(rule['path']);require(not rel.is_absolute() and '..' not in rel.parts,'Unsafe manifest path');path=root/rel
        require(os.path.commonpath((root,path.resolve(strict=True)))==str(root) and not path.is_symlink(),'Unsafe source path')
        data=path.read_bytes();out=data if args.verify else transform(data,rule)
        require(sha(out)==rule['output_sha256'],'Unexpected output hash: '+str(rel));elf=Elf(out)
        require(not elf.nonw() and 22 not in elf.d and not elf.d.get(30,0)&4,'TEXTREL remains');pending.append((path,out))
    if args.apply:
        for path,out in pending:
            name=None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.pic-',delete=False) as fp:
                    name=fp.name;os.fchmod(fp.fileno(),path.stat().st_mode&0o777);fp.write(out);fp.flush();os.fsync(fp.fileno())
                os.replace(name,path)
            finally:
                if name and os.path.exists(name):os.unlink(name)
    print('Validated',len(rules),'PIC startup/JPEG files')
if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,OSError,struct.error) as e:raise SystemExit('TEXTREL extra fixup failed: '+str(e))
