#!/usr/bin/env bash
# The mechanical part of the periodic code review (see the zelda-wide-review skill): stricter compiler warnings over every
# source, then dead-code candidates. Prints only findings.
root="$(cd "$(dirname "$0")/.." && pwd)"
echo "== strict warnings (shadowing, logic, null derefs, missing declarations)"
n=0
for f in "$root"/src/*.cpp; do
  out="$(g++ -std=c++17 -O2 -fsyntax-only -Wall -Wextra -Wshadow -Wlogical-op -Wduplicated-cond -Wduplicated-branches \
      -Wnull-dereference -Wuseless-cast -Wmissing-declarations -Wcast-qual -Wundef -I"$root/src" -I"$root/tools/sdl_stub" "$f" 2>&1 | grep "warning")"
  [ -n "$out" ] && { echo "$out" | sed "s|$root/||" | cut -c1-170; n=$((n+1)); }
done
[ $n -eq 0 ] && echo "(none)"
echo "== dead-code candidates"
python3 "$root/tools/review_deadcode.py"
