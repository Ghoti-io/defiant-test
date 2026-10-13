#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# The unsafe-list lint (AD-18 items 1-5) over the real source of every Defiant
# library, and a planted construct it is seen to catch.
#
# 1. LintTree.Control: the walker finds a planted construct of each item in a
#    tree it builds. Without it a walker that reads nothing looks clean.
# 2. LintTree.RealSource over the real roots must pass.
# 3. For each item, an unmarked construct is inserted into a scratch copy of a
#    real source file, and the same test over the copy must fail by name and
#    name the file, line and item. The edit must change the file.
# 4. The same construct with the marker must pass: the plant is the missing
#    marker, nothing else.
#
# The tokens are written in halves so that this script is itself clean.
#
# Usage: check-lint.sh <testLinttree> <root>...

set -u

BIN="${1:?usage: check-lint.sh <testLinttree> <root>...}"
shift
if [ "$#" -lt 1 ]; then
  printf 'check-lint: no source roots given\n' >&2
  exit 1
fi

for r in "$@"; do
  if [ ! -d "$r/src" ]; then
    printf 'check-lint: %s has no src/. Check out every Defiant library beside this one, or set DEFIANT_LINT_ROOTS.\n' "$r" >&2
    exit 1
  fi
done

SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
fail=0

T1='unescaped-''output'
T2='unfiltered-''read'
T3='string-''query'
T4='unsigned-''form'
T5='direct-''storage'

sep=:
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) sep=\; ;; esac

join_roots() {
  out=
  for r in "$@"; do
    if [ -z "$out" ]; then out=$r; else out="$out$sep$r"; fi
  done
  printf '%s' "$out"
}

if ! "$BIN" --gtest_filter='LintTree.Control' --gtest_brief=1 > "$SCRATCH/control.log" 2>&1; then
  printf 'check-lint: the walker did not find its own planted constructs; a clean run over source would mean nothing\n' >&2
  tail -20 "$SCRATCH/control.log" >&2
  exit 1
fi
printf 'check-lint: control: the walker found a planted construct of each of the five items\n'

if ! GDEFTEST_LINT_ROOTS=$(join_roots "$@") "$BIN" \
    --gtest_filter='LintTree.RealSource' --gtest_brief=1 > "$SCRATCH/real.log" 2>&1; then
  printf 'check-lint: the real source is not clean\n' >&2
  cat "$SCRATCH/real.log" >&2
  exit 1
fi
grep '^LintTree.RealSource:' "$SCRATCH/real.log" | sed 's/^/check-lint: /'
printf 'check-lint: %s roots are clean\n' "$#"

# plant <item> <token> <root index (1-based)> <marked: 0|1>
plant() {
  item=$1 token=$2 idx=$3 marked=$4
  i=0
  roots=
  victim=
  set -- $ROOTS
  n=$#
  for r in "$@"; do
    i=$((i + 1))
    if [ "$i" -eq "$idx" ]; then
      name=$(basename "$r")
      dest="$SCRATCH/$item.$marked/$name"
      mkdir -p "$dest"
      ( cd "$r" && tar --exclude=build --exclude=.git -cf - . ) | ( cd "$dest" && tar -xf - )
      file=$(cd "$dest" && find src -name '*.c' | sort | head -1)
      if [ -z "$file" ]; then
        printf 'check-lint: item %s: %s has no src/*.c to plant in\n' "$item" "$r" >&2
        fail=1
        return
      fi
      victim="$dest/$file"
      if [ "$marked" -eq 1 ]; then
        line="/* unsafe: $token */"
      else
        line="/* $token */"
      fi
      before=$(cksum < "$victim")
      LINE="$line" python3 - "$victim" <<'PY'
import os, sys
path = sys.argv[1]
lines = open(path).read().split("\n")
lines.insert(1, os.environ["LINE"])
open(path, "w").write("\n".join(lines))
PY
      after=$(cksum < "$victim")
      if [ "$before" = "$after" ]; then
        printf 'check-lint: item %s: THE EDIT DID NOT CHANGE %s, nothing was tested\n' "$item" "$victim" >&2
        fail=1
        return
      fi
      roots="$roots${roots:+$sep}$dest"
    else
      roots="$roots${roots:+$sep}$r"
    fi
  done
  GDEFTEST_LINT_ROOTS=$roots "$BIN" --gtest_filter='LintTree.RealSource' \
    --gtest_brief=1 > "$SCRATCH/$item.$marked.log" 2>&1
  rc=$?
  if [ "$marked" -eq 1 ]; then
    if [ $rc -ne 0 ]; then
      printf 'check-lint: item %s: the MARKED construct was refused\n' "$item" >&2
      tail -10 "$SCRATCH/$item.$marked.log" >&2
      fail=1
    else
      printf 'check-lint: item %s: the same construct with the marker passes\n' "$item"
    fi
    return
  fi
  if [ $rc -eq 0 ]; then
    printf 'check-lint: item %s: the unmarked construct PASSED\n' "$item" >&2
    fail=1
  elif grep -q '^\[  FAILED  \] LintTree.RealSource' "$SCRATCH/$item.$marked.log" &&
       grep -q "$victim:2: item $item" "$SCRATCH/$item.$marked.log"; then
    printf 'check-lint: item %s: planted in %s turned LintTree.RealSource red (%s:2: item %s)\n' \
      "$item" "$file" "$(basename "$victim")" "$item"
  else
    printf 'check-lint: item %s: red, but not by LintTree.RealSource at the planted line\n' "$item" >&2
    tail -10 "$SCRATCH/$item.$marked.log" >&2
    fail=1
  fi
}

ROOTS=$(join_roots "$@")
# Split on the separator for the helper, which indexes roots.
ROOTS=$(printf '%s' "$ROOTS" | tr "$sep" ' ')
count=$#
k=0
for item in 1 2 3 4 5; do
  eval "token=\$T$item"
  k=$((k % count + 1))
  plant "$item" "$token" "$k" 0
done
plant 1 "$T1" 1 1

if [ "$fail" -ne 0 ]; then
  exit 1
fi
printf 'check-lint: every planted unmarked construct was seen to fail the real-source lint\n'
