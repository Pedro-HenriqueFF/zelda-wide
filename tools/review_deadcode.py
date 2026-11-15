#!/usr/bin/env python3
"""Dead-code candidates: identifiers declared in src/*.h that are used (almost) nowhere.
Prints UNUSED (appears once: only its declaration) and, on one line, the names that appear exactly twice (declaration + one use
or declaration + definition) so a reviewer can glance at them. Run from anywhere: tools/review_deadcode.py"""
import re, glob, os, collections
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
files = glob.glob(root + '/src/*') + glob.glob(root + '/tests/*.cpp') + glob.glob(root + '/tests/*.h')
text = {f: open(f, errors='ignore').read() for f in files}
cnt = collections.Counter(re.findall(r'[A-Za-z_]\w*', '\n'.join(text.values())))
ids = set()
for h in glob.glob(root + '/src/*.h'):
    t = re.sub(r'//.*', '', text[h])
    ids |= set(re.findall(r'\b([A-Za-z_]\w*)\s*(?:\(|=|;|\{|\[)', t))
kw = set('if for while switch return sizeof else do case static const int bool void float double char uint8_t uint16_t uint32_t '
         'int8_t int16_t int32_t uint64_t size_t struct class enum namespace using inline constexpr unsigned auto override public private sqrt'.split())
unused = sorted(i for i in ids if i not in kw and cnt[i] <= 1)
once = sorted(i for i in ids if i not in kw and cnt[i] == 2)
print('UNUSED:', ', '.join(unused) if unused else '(none)')
print('used once (glance at these):', ', '.join(once) if once else '(none)')
