# Compares every `kName = 0x..` in src/zelda_ram.h with the same-name variable in the disassembly's Variables.inc / CaveVars.inc / ObjVars.inc.
# Usage: python3 tools/ramcmp.py [path to zelda1-disassembly-master/src]   (names without a match are listed: check those by hand)
import re,sys,os
DIS = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/zelda1-disassembly-master/src'
inc={}
for f in ['Variables.inc','CaveVars.inc','ObjVars.inc']:
    for l in open(os.path.join(DIS,f),encoding='latin1'):
        m=re.match(r'\s*(\w+)\s*:=\s*\$([0-9A-Fa-f]+)',l)
        if m: inc.setdefault(m.group(1),set()).add(int(m.group(2),16))
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','src','zelda_ram.h')).read()
ok=mis=0
for m in re.finditer(r'\b(k[A-Za-z0-9]+)\s*=\s*(0x[0-9A-Fa-f]+)',src):
    name=m.group(1)[1:]; val=int(m.group(2),16)
    if name in inc:
        if val in inc[name]: ok+=1
        else: mis+=1; print('MISMATCH',name,hex(val),[hex(x) for x in inc[name]])
    else:
        print('noname',name,hex(val),[n for n,v in inc.items() if val in v][:4])
print('ok',ok,'mismatch',mis)
