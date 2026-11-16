#!/bin/sh
# Puts a new milestone zip over this project folder WITHOUT touching the files that did not change, so the next build only
# recompiles what is new (unzipping over the folder gives every file a new date, which makes the compiler rebuild everything).
#   tools/linux-update.sh ~/Downloads/zelda-wide-milestone100.zip          # update the files
#   tools/linux-update.sh ~/Downloads/zelda-wide-milestone100.zip --build  # ... and build + test afterwards
# Linux counterpart of tools/win-update.ps1. Needs unzip or python3.
set -eu
if [ $# -lt 1 ] || [ ! -f "$1" ]; then
  echo "usage: $0 path/to/zelda-wide-milestoneN.zip [--build]" >&2
  exit 2
fi
zip=$1; build=${2:-}
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/zw-update.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
if command -v unzip >/dev/null 2>&1; then unzip -q "$zip" -d "$tmp"
else python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$zip" "$tmp"; fi
src=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)
if [ -z "$src" ] || [ ! -f "$src/CMakeLists.txt" ]; then echo "this does not look like a Zelda Wide zip (no CMakeLists.txt)" >&2; exit 1; fi

changed=0; same=0; removed=0
list=$(cd "$src" && find . -type f | sort)
for rel in $list; do
  if [ -f "$root/$rel" ] && cmp -s "$src/$rel" "$root/$rel"; then same=$((same + 1)); continue; fi
  mkdir -p "$(dirname "$root/$rel")"
  cp -p "$src/$rel" "$root/$rel"
  touch "$root/$rel"                       # (a new date, so that make/ninja rebuild exactly this file)
  changed=$((changed + 1)); echo "  updated ${rel#./}"
done
# files this version no longer has (only in the source folders: never the build folder or your own files)
for dir in src tests tools docs; do
  [ -d "$root/$dir" ] || continue
  for f in $(cd "$root" && find "$dir" -type f | sort); do
    if [ ! -f "$src/$f" ]; then rm -f "$root/$f"; removed=$((removed + 1)); echo "  removed $f"; fi
  done
done
chmod +x "$root"/tools/*.sh 2>/dev/null || true
echo "$changed files updated, $same unchanged, $removed removed."
if [ "$build" = "--build" ]; then
  cd "$root" && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure
else
  echo "Now build:  cmake --build build -j    (first time: cmake -B build)"
fi
