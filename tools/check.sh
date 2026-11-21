#!/usr/bin/env bash
# One command for everything that has to be green before a milestone is delivered:
#   - builds the library + the three test programs and runs them (fast -O2 build by default; --san or --zip = with AddressSanitizer/UBSan)
#   - syntax-checks src/main.cpp against the SDL stub (main.cpp can't be built without SDL2)
#   - incremental: only changed files are recompiled (objects live in /tmp/zw-build)
#   - optionally zips the project:  tools/check.sh --zip 18 (--zip-only 18 = zip without checks, for doc-only changes)   ->  zelda-wide-milestone18.zip in $ZW_ZIP_DIR (default: /mnt/user-data/outputs if writable, else ./dist)
# Output is deliberately short: one line per test program, plus only the FAIL lines and compiler errors/warnings.
root="$(cd "$(dirname "$0")/.." && pwd)"
# Two modes: FAST (default, -O2, no sanitizers, objects in /tmp/zw-fast: test_wide runs in ~20 s instead of ~100 s) for the edit-test loop, and
# SANITIZED (-O1 + AddressSanitizer/UBSan, objects in /tmp/zw-build) with --san or --zip N (the delivery run).
case " $* " in *" --san "*|*" --zip "*) mode=san;; *) mode=fast;; esac
[ "${1:-}" = "--san" ] && shift
if [ $mode = san ]; then B=/tmp/zw-build; F="-std=c++17 -O1 -pthread -Wall -Wextra -fsanitize=address,undefined -I$root/src"
else B=/tmp/zw-fast; F="-std=c++17 -O2 -pthread -Wall -Wextra -I$root/src"; fi
mkdir -p "$B"
status=0
# --zip-only N: just zip (use it when only docs changed since the last green run).
if [ "${1:-}" = "--zip-only" ]; then set -- --zip "$2"; skipchecks=1; fi
# Incremental: an object is rebuilt only when its source or a header it includes is newer (.d files from -MMD).
build_obj() {   # $1 = source, $2 = object; returns 0 if ok
  local src="$1" o="$2" d="${2%.o}.d"
  if [ -f "$o" ] && [ -f "$d" ]; then
    local stale="" f
    for f in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$d" | tr -s ' \n' '\n\n'); do [ "$f" -nt "$o" ] && { stale=1; break; }; done
    [ -z "$stale" ] && return 0
  fi
  g++ $F -MMD -MF "$d" -c "$src" -o "$o" 2> "${o%.o}.log" || { echo "BUILD FAILED: $(basename "$src")"; grep -E "error" "${o%.o}.log" | head -10; rm -f "$o"; return 1; }
  grep -E "warning" "${o%.o}.log" | grep -v "variable tracking" | head -5
  return 0
}
if [ -z "${skipchecks:-}" ]; then
for src in "$root"/src/*.cpp; do
  f="$(basename "$src" .cpp)"; [ "$f" = main ] && continue       # (every src/*.cpp except main.cpp is part of the library)
  build_obj "$src" "$B/lib_$f.o" || status=1
done
for src in "$root"/tests/*.cpp; do build_obj "$src" "$B/t_$(basename "$src" .cpp).o" || status=1; done
[ $status -ne 0 ] && exit 1
for t in test_core test_world test_wide; do
  objs=("$B/t_$t.o"); [ "$t" = test_wide ] && objs=("$B"/t_test_wide*.o)     # (test_wide is several files)
  g++ $F "${objs[@]}" "$B"/lib_*.o -o "$B/$t" 2> "$B/$t.log" || { echo "BUILD FAILED (link): $t"; grep -E "error|undefined" "$B/$t.log" | head -10; status=1; continue; }
  out="$("$B/$t" 2>&1)"; rc=$?
  echo "$t: $(echo "$out" | tail -1)"
  echo "$out" | grep -E "FAIL|ERROR|runtime error" | head -20
  [ $rc -ne 0 ] && status=1
done
fi
if [ -z "${skipchecks:-}" ]; then
# CMake can't be run in the sandbox: at least make sure every library source is listed in CMakeLists.txt.
for src in "$root"/src/*.cpp; do
  f="$(basename "$src")"; [ "$f" = main.cpp ] && continue
  grep -q "src/$f" "$root/CMakeLists.txt" || { echo "CMakeLists.txt is missing src/$f (the Windows build would fail)"; status=1; }
done
for src in "$root"/tests/*.cpp; do
  f="$(basename "$src")"
  grep -q "tests/$f" "$root/CMakeLists.txt" || { echo "CMakeLists.txt is missing tests/$f (test link errors on Windows)"; status=1; }
done
if g++ -std=c++17 -Wall -Wextra -fsyntax-only -I"$root/tools/sdl_stub" -I"$root/src" "$root/src/main.cpp" 2> "$B/main.log"; then
  echo "main.cpp OK"; grep -E "warning" "$B/main.log" | head -5
else
  echo "main.cpp FAILED"; grep -E "error" "$B/main.log" | head -10; status=1
fi
fi
if [ "${1:-}" = "--zip" ] && [ -n "${2:-}" ]; then
  if [ $status -ne 0 ]; then echo "not zipping: checks failed"; exit 1; fi
  python3 - "$root" "$2" << 'PY'
import sys, os, zipfile
root, n = sys.argv[1], sys.argv[2]
# where the zip goes: $ZW_ZIP_DIR, else the sandbox's /mnt/user-data/outputs when it can be written, else <project>/dist
d = os.environ.get('ZW_ZIP_DIR') or ('/mnt/user-data/outputs' if os.access('/mnt/user-data/outputs', os.W_OK) else os.path.join(root, 'dist'))
out = os.path.join(d, f'zelda-wide-milestone{n}.zip')
os.makedirs(d, exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for d, dirs, fs in os.walk(root):
        # only the project: not build output, downloaded SDL2 (Windows), earlier zips, docs/images, and never a ROM or what the program writes next to it
        dirs[:] = [x for x in dirs if x not in ('build', '.git', 'dist', 'third_party') and os.path.join(d, x) != os.path.join(root, 'docs', 'images')]   # (the README's screenshots stay in the repo, not in the zips)
        for f in fs:
            if not f.endswith(('.tmp', '.zip', '.nes', '.sav', '.fog', '.perf.txt')):
                p = os.path.join(d, f); z.write(p, os.path.join('zelda-wide', os.path.relpath(p, root)))
print('zipped:', out)
PY
fi
exit $status
