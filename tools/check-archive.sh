#!/bin/sh
#
# SPDX-License-Identifier: LGPL-3.0-only
#
# The hostile-archive gate, with a plant for each hostile archive.
#
# The tests (Site.Traversal, Bomb, NamedTenant, InternalId, Installation,
# Mistype) hand gdefcore_subject_restore a zip that is hostile in one
# way each and require it refused with nothing stored. A test that has never
# been seen to fail proves nothing, so this script copies the two libraries
# that judge an archive, removes one refusal by an exact text edit,
# rebuilds the copy, and requires THAT test to fail by name.
#
# Nothing in these libraries extracts an archive to the filesystem, so the
# AD-25 plant "a member path joined to the filesystem" is made literally: the
# traversal plant adds the join to a scratch copy of defiant-core's archive
# walk, ahead of the name check. Site.Traversal runs the import in an empty
# directory inside an empty directory and requires that neither gains a file.
# Deleting the ".." refusal alone does not turn the test red, and the script
# does not pretend it does: three layers refuse a "../x" member (the name
# check in defiant-core, the one in defiant-storage, and the manifest the
# snapshot reader requires), and all of them answer GDEFCORE_ERR_CORRUPT.
#
# The plants are edits to scratch copies. Nothing is planted in the
# libraries, and an edit that does not apply exactly once fails the script.
#
# Usage: check-archive.sh <test_harness> <defiant-core-src> <defiant-storage-src>
#        <make> [make args...]
# Run with LD_LIBRARY_PATH and PKG_CONFIG_PATH as for the test itself.

set -u

BIN="${1:?usage: check-archive.sh <test_harness> <core-src> <storage-src> <make> [args]}"
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
CORE="${2:?missing the defiant-core source tree}"
STOR="${3:?missing the defiant-storage source tree}"
MAKE_CMD="${4:?missing make}"
shift 4
MARGS="-j${GHOTI_CHECK_JOBS:-4} $*"

for d in "$CORE" "$STOR"; do
  if [ ! -f "$d/Makefile" ] || [ ! -d "$d/src" ]; then
    printf 'check-archive: %s is not a library source tree. Check out defiant-core and defiant-storage beside this library, or set DEFIANT_CORE_SRC and DEFIANT_STORAGE_SRC.\n' "$d" >&2
    exit 1
  fi
done

unset MAKEFLAGS MFLAGS
ROOT=$(pwd)
SCRATCH="$ROOT/build/archive-plants"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
trap 'rm -rf "$SCRATCH"' EXIT

ALL='Site.Traversal:Site.Bomb:Site.NamedTenant:Site.InternalId:Site.Installation:Site.Mistype'

if ! "$BIN" --gtest_filter="$ALL" --gtest_brief=1 > "$SCRATCH/real.log" 2>&1; then
  printf 'check-archive: the hostile-archive tests fail on the real tree\n' >&2
  tail -30 "$SCRATCH/real.log" >&2
  exit 1
fi
# A filter that matches nothing also exits 0. Count what ran.
ran=$("$BIN" --gtest_filter="$ALL" --gtest_list_tests | grep -c '^  ')
if [ "$ran" -ne 6 ]; then
  printf 'check-archive: expected 6 hostile-archive tests, found %s\n' "$ran" >&2
  exit 1
fi
printf 'check-archive: the 6 hostile-archive tests pass on the real tree\n'

fail=0

copy_tree() {
  src=$1
  dest=$2
  mkdir -p "$dest"
  for d in $(ls -A "$src"); do
    case "$d" in build | .git | docs) continue ;; esac
    cp -a "$src/$d" "$dest/"
  done
}

# edit <dir> <file> <old> <new>: replace the one occurrence, or return 3.
edit() {
  python3 - "$1/$2" "$3" "$4" <<'PY'
import sys
path, old, new = sys.argv[1:4]
text = open(path).read()
if text.count(old) != 1:
    sys.stderr.write("anchor matched %d times in %s\n" % (text.count(old), path))
    sys.exit(3)
open(path, "w").write(text.replace(old, new, 1))
PY
}

# The directory a library's make puts its shared object in.
app_dir() {
  ( cd "$1" && "$MAKE_CMD" -pqn $MARGS 2>/dev/null | sed -n 's/^APP_DIR := //p' | head -1 | { read -r d; printf '%s/%s' "$(pwd)" "$d"; } )
}

# plant <name> <test> <which: core|storage|both> <file-in-core> <old> <new>
# The storage edit, when both, is given as <storage-old> <storage-new>.
build_and_run() {
  name=$1 test=$2 core_dir=$3 stor_dir=$4
  libpath="$(app_dir "$core_dir"):$(app_dir "$stor_dir")"
  for d in "$stor_dir" "$core_dir"; do
    if ! ( cd "$d" && "$MAKE_CMD" --no-print-directory all $MARGS ) \
        > "$SCRATCH/$name.build.log" 2>&1; then
      printf 'check-archive: %s: the planted copy did not build\n' "$name" >&2
      tail -20 "$SCRATCH/$name.build.log" >&2
      fail=1
      return 1
    fi
  done
  # Run from a directory of its own: the join plant creates files.
  mkdir -p "$SCRATCH/cwd"
  ( cd "$SCRATCH/cwd" && LD_LIBRARY_PATH="$libpath:${LD_LIBRARY_PATH:-}" \
    "$BIN" --gtest_filter="$ALL" --gtest_brief=1 > "$SCRATCH/$name.log" 2>&1 )
  rc=$?
  if [ $rc -eq 0 ]; then
    printf 'check-archive: %s: every test PASSED with the defect planted\n' "$name" >&2
    fail=1
  elif grep -q "^\[  FAILED  \] $test\b" "$SCRATCH/$name.log"; then
    printf 'check-archive: %s: planted defect turned %s red\n' "$name" "$test"
  else
    printf 'check-archive: %s: red, but not by %s (exit %s)\n' "$name" "$test" "$rc" >&2
    tail -15 "$SCRATCH/$name.log" >&2
    fail=1
  fi
}

# Scratch copies are made once per plant. Each is built against the real
# prefix and loaded ahead of it through LD_LIBRARY_PATH.
plant() {
  name=$1 test=$2 core_old=$3 core_new=$4 stor_old=$5 stor_new=$6
  cdir="$SCRATCH/$name/defiant-core"
  sdir="$SCRATCH/$name/defiant-storage"
  copy_tree "$CORE" "$cdir"
  copy_tree "$STOR" "$sdir"
  if ! edit "$cdir" src/subject/subject.c "$core_old" "$core_new"; then
    printf 'check-archive: %s: THE CORE EDIT DID NOT APPLY, nothing was tested\n' "$name" >&2
    fail=1
    return
  fi
  if cmp -s "$CORE/src/subject/subject.c" "$cdir/src/subject/subject.c"; then
    printf 'check-archive: %s: the edited core file equals the original\n' "$name" >&2
    fail=1
    return
  fi
  if [ -n "$stor_old" ]; then
    if ! edit "$sdir" src/snapshot.c "$stor_old" "$stor_new"; then
      printf 'check-archive: %s: THE STORAGE EDIT DID NOT APPLY, nothing was tested\n' "$name" >&2
      fail=1
      return
    fi
    if cmp -s "$STOR/src/snapshot.c" "$sdir/src/snapshot.c"; then
      printf 'check-archive: %s: the edited storage file equals the original\n' "$name" >&2
      fail=1
      return
    fi
  fi
  build_and_run "$name" "$test" "$cdir" "$sdir"
  rm -rf "$SCRATCH/$name"
}

RATIO='return uncomp > (uint64_t)max_ratio * comp;'

# The member path is joined to the filesystem before it is judged. The name
# is made into "./<name>" and created, which is what an extractor does.
plant join Site.Traversal \
  '    if (member->type != GARC_MEMBER_FILE ||
        name_bad(member->name, member->name_length)) {' \
  '    {
      char joined[512];
      FILE * created;
      snprintf(joined, sizeof joined, "./%.*s", (int)member->name_length,
          member->name);
      created = fopen(joined, "wb");
      if (created != NULL) {
        fclose(created);
      }
    }
    if (member->type != GARC_MEMBER_FILE ||
        name_bad(member->name, member->name_length)) {' '' ''

# The compression ratio is not judged.
plant bomb Site.Bomb "$RATIO" 'return 0;' "$RATIO" 'return 0;'

# A document that names a tenant is accepted.
plant named-tenant Site.NamedTenant \
  '  if (gtext_json_object_get(root, "tenant", 6) != NULL) {
    return GDEFCORE_ERR_INVALID;
  }' \
  '  if (gtext_json_object_get(root, "tenant", 6) != NULL && 0) {
    return GDEFCORE_ERR_INVALID;
  }' '' ''

# An internal (decimal) id is accepted: the digits check and the canonical
# public-id check both go, because the second also refuses "7".
plant internal-id Site.InternalId \
  '  if (digits_only(entity_text, entity_len)) {
    return GDEFCORE_ERR_INVALID;
  }
  if (gdefcore_id_canonical(entity_text, entity_len, canon) != GDEFCORE_OK) {
    return GDEFCORE_ERR_INVALID;
  }' \
  '  (void)canon;' '' ''

# A mistyped public id is accepted.
plant mistype Site.Mistype \
  '  if (gdefcore_id_canonical(entity_text, entity_len, canon) != GDEFCORE_OK) {
    return GDEFCORE_ERR_INVALID;
  }' \
  '  (void)canon;' '' ''

# An installation-scope type is accepted.
plant installation Site.Installation \
  'span_is(type_text, type_len, "core.tenant")' \
  'span_is(type_text, type_len, "core.tenan_")' '' ''

if [ "$fail" -ne 0 ]; then
  exit 1
fi
printf 'check-archive: every hostile-archive plant was seen to fail its test\n'
