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
# Speed. A copy takes its tree's finished build (without the test programs),
# with its timestamps, and is built with the flags its tree was: the test data
# path is handed to it as a make argument, so the copy compiles the objects
# its edit reaches and relinks, not the library. Plants run at once (up to
# GHOTI_CHECK_JOBS, default at most four), each in copies of its own, and
# print in the order they are written. Each shows that its edit was compiled
# in: an object that depends on the edit and was not compiled is a STALE
# OBJECT, a failure of its own. The last plant makes the rebuild not happen
# and has to be refused as one (skipped when there is no build to be stale).
# The helpers are defiant-storage's plant-lib.sh.
#
# Usage: check-archive.sh <test_harness> <defiant-core-src> <defiant-storage-src>
#        <make> <build-dir> [make args...]
# Run with LD_LIBRARY_PATH and PKG_CONFIG_PATH as for the test itself, and
# GHOTI_PLANT_LIB naming plant-lib.sh.

set -u

BIN="${1:?usage: check-archive.sh <test_harness> <core-src> <storage-src> <make> <build-dir> [args]}"
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
CORE="${2:?missing the defiant-core source tree}"
STOR="${3:?missing the defiant-storage source tree}"
MAKE_CMD="${4:?missing make}"
BUILD_DIR="${5:?missing the build directory}"
shift 5

for d in "$CORE" "$STOR"; do
  if [ ! -f "$d/Makefile" ] || [ ! -d "$d/src" ]; then
    printf 'check-archive: %s is not a library source tree. Check out defiant-core and defiant-storage beside this library, or set DEFIANT_CORE_SRC and DEFIANT_STORAGE_SRC.\n' "$d" >&2
    exit 1
  fi
done
case "$CORE" in /*) ;; *) CORE="$(cd "$CORE" && pwd)" ;; esac
case "$STOR" in /*) ;; *) STOR="$(cd "$STOR" && pwd)" ;; esac

. "$(dirname "$0")/plant-load.sh"
plant_setup "$MAKE_CMD" "$BUILD_DIR" "$@"
SCRATCH=$PLANT_SCRATCH

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

# copy_lib <tree> <dest>: the tree as git tracks it, and its finished build
# without the test objects and the test programs, which a library build never
# reads. Timestamps are kept, so make recompiles what an edit reaches.
copy_lib() {
  src=$1 dest=$2
  mkdir -p "$dest"
  for d in $(ls -A "$src"); do
    case "$d" in build | .git | docs) continue ;; esac
    plant_cp "$src/$d" "$dest/"
  done
  [ -z "${GHOTI_CHECK_COLD:-}" ] || return 0
  b="$src/$BUILD_DIR"
  [ -d "$b" ] || return 0
  mkdir -p "$dest/$BUILD_DIR"
  for e in "$b"/* "$b"/.[!.]*; do
    [ -e "$e" ] || continue
    case "${e##*/}" in
      objects)
        mkdir -p "$dest/$BUILD_DIR/objects"
        for o in "$e"/* "$e"/.[!.]*; do
          [ -e "$o" ] || continue
          [ "${o##*/}" = tests ] && continue
          plant_cp "$o" "$dest/$BUILD_DIR/objects/"
        done
        ;;
      apps)
        mkdir -p "$dest/$BUILD_DIR/apps"
        for a in "$e"/* "$e"/.[!.]*; do
          [ -e "$a" ] || continue
          case "${a##*/}" in test* | readzip) ;; *) plant_cp "$a" "$dest/$BUILD_DIR/apps/" ;; esac
        done
        ;;
      *) plant_cp "$e" "$dest/$BUILD_DIR/" ;;
    esac
  done
}

# edit_and_build <label> <tree> <dest> <stale> <file> <old> <new>: copy the
# tree, apply the one exact edit, build the libraries. With <stale> = 1 the
# objects that depend on the edit are made to look up to date first, so the
# rebuild does not happen. Leaves what the stale-object check found in
# STALE_OBJS; returns 1 (having said why) if the plant cannot be tested.
edit_and_build() {
  label=$1 tree=$2 dest=$3 stale=$4 file=$5 old=$6 new=$7
  copy_lib "$tree" "$dest"
  if ! plant_edit p "$dest" "$file" "$old" "$new"; then
    printf 'check-archive: %s: THE %s EDIT DID NOT APPLY, nothing was tested\n' "$name" "$label" >&2
    return 1
  fi
  if cmp -s "$tree/$file" "$dest/$file"; then
    printf 'check-archive: %s: the edited %s file equals the original\n' "$name" "$label" >&2
    return 1
  fi
  touch "$dest/$file"
  if [ "$stale" = 1 ]; then
    for o in $(plant_depends "$dest" "$file"); do
      ( cd "$dest" && touch "$o" )
    done
  fi
  # shellcheck disable=SC2086
  if ! ( cd "$dest" && "$MAKE_CMD" --no-print-directory all $PLANT_PLANT_ARGS \
      TEST_DATA="$tree/tests/data" ) > "$dest.build.log" 2>&1; then
    printf 'check-archive: %s: the planted copy did not build\n' "$name" >&2
    tail -20 "$dest.build.log" >&2
    return 1
  fi
  more=$(plant_stale_objects "$dest" "$dest.build.log" "$file")
  case "$more" in UNLISTED*) printf 'check-archive: %s: no object depends on %s, so no rebuild could show the edit: %s\n' "$name" "$file" "$more" >&2; return 1 ;; esac
  if [ -n "$more" ]; then
    STALE_OBJS="${STALE_OBJS:+$STALE_OBJS
}$more"
  fi
  return 0
}

# archive_job <name> <test> <stale> <core-old> <core-new> <storage-old> <storage-new>
# One plant, in a subshell of its own (plant_spawn): copies of the libraries
# whose source it edits, built, and the hostile-archive tests run against
# them. Leaves 0 for a plant that behaved.
archive_job() {
  name=$1 test=$2 stale=$3 core_old=$4 core_new=$5 stor_old=$6 stor_new=$7
  dir="$PLANT_SCRATCH/$name"
  mkdir -p "$dir/cwd"
  libpath=
  STALE_OBJS=
  files=
  if [ -n "$core_old" ]; then
    edit_and_build core "$CORE" "$dir/defiant-core" "$stale" src/subject/subject.c \
      "$core_old" "$core_new" || return 1
    libpath="$dir/defiant-core/$BUILD_DIR/apps:$libpath"
    files="$files src/subject/subject.c"
  fi
  if [ -n "$stor_old" ]; then
    edit_and_build storage "$STOR" "$dir/defiant-storage" "$stale" src/snapshot.c \
      "$stor_old" "$stor_new" || return 1
    libpath="$dir/defiant-storage/$BUILD_DIR/apps:$libpath"
    files="$files src/snapshot.c"
  fi
  if [ "$stale" = 1 ]; then
    if [ -n "$STALE_OBJS" ]; then
      printf 'check-archive: %s: caught by the stale-object check:%s edited, and not rebuilt:\n' "$name" "$files"
      printf '%s\n' "$STALE_OBJS" | sed 's/^/check-archive:   /'
      rm -rf "$dir"
      return 0
    fi
    printf 'check-archive: %s: every object that depends on the edit was rebuilt anyway, so the stale-object check was not shown to work\n' "$name" >&2
    return 1
  fi
  if [ -n "$STALE_OBJS" ]; then
    printf 'check-archive: %s: STALE OBJECT: the edit was not rebuilt into the library, so the defect never reached the tests:\n' "$name" >&2
    printf '%s\n' "$STALE_OBJS" | sed 's/^/check-archive:   /' >&2
    return 1
  fi
  # Run from a directory of its own: the join plant creates files.
  ( cd "$dir/cwd" && LD_LIBRARY_PATH="$libpath${LD_LIBRARY_PATH:-}" \
    "$BIN" --gtest_filter="$ALL" --gtest_brief=1 > "$dir/run.log" 2>&1 )
  rc=$?
  status=0
  if [ $rc -eq 0 ]; then
    printf 'check-archive: %s: every test PASSED with the defect planted\n' "$name" >&2
    status=1
  elif grep -q "^\[  FAILED  \] $test\b" "$dir/run.log"; then
    printf 'check-archive: %s: planted defect turned %s red\n' "$name" "$test"
  else
    printf 'check-archive: %s: red, but not by %s (exit %s)\n' "$name" "$test" "$rc" >&2
    tail -15 "$dir/run.log" >&2
    status=1
  fi
  rm -rf "$dir"
  return $status
}

plant_hook_done() {
  printf 'check-archive: every hostile-archive plant was seen to fail its test\n'
}

# plant <name> <test> <core-old> <core-new> <storage-old> <storage-new>
plant() {
  plant_spawn archive_job "$1" "$2" 0 "$3" "$4" "$5" "$6"
}

RATIO='return uncomp > (uint64_t)max_ratio * comp;'

# The member path is joined to the filesystem before it is judged. The name
# is made into "./<name>" and created, which is what an extractor does.
JOIN_OLD='    if (member->type != GARC_MEMBER_FILE ||
        name_bad(member->name, member->name_length)) {'
JOIN_NEW='    {
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
        name_bad(member->name, member->name_length)) {'
plant join Site.Traversal "$JOIN_OLD" "$JOIN_NEW" '' ''

# The compression ratio is not judged.
plant bomb Site.Bomb "$RATIO" 'return 0;' "$RATIO" 'return 0;'

# A document that names a tenant is accepted.
TENANT_OLD='  if (gtext_json_object_get(root, "tenant", 6) != NULL) {
    return GDEFCORE_ERR_INVALID;
  }'
TENANT_NEW='  if (gtext_json_object_get(root, "tenant", 6) != NULL && 0) {
    return GDEFCORE_ERR_INVALID;
  }'
plant named-tenant Site.NamedTenant "$TENANT_OLD" "$TENANT_NEW" '' ''

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

# The stale-object plant. A copy starts from its tree's finished build, so the
# edit has to be compiled into it; this one (the named-tenant edit) is made not
# to be, and the gate has to refuse it as a STALE OBJECT. If it ever passes
# instead, a warm copy could pass with the defect never built in. There is
# nothing to be stale when the library has no build to copy.
if [ -n "${GHOTI_CHECK_COLD:-}" ]; then
  plant_say "check-archive: stale-object: SKIPPED: GHOTI_CHECK_COLD copies no build output, so no object can be stale"
elif [ ! -d "$CORE/$BUILD_DIR/objects" ]; then
  plant_say "check-archive: stale-object: SKIPPED: $CORE has no build in $BUILD_DIR, so no object can be stale"
else
  plant_spawn archive_job stale-object Site.NamedTenant 1 "$TENANT_OLD" "$TENANT_NEW" '' ''
fi

plant_finish
