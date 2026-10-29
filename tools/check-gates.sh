#!/bin/sh
#
# The milestone gates.
#
# Each plant lives in the test. The library does the safe thing, so the
# planted run turns the gate red. Unset, the same test passes.
#
# Usage: check-gates.sh <test_harness-binary>

set -eu

BIN="${1:?usage: check-gates.sh <test_harness>}"

pass() {
  filter=$1
  if ! env -u GHOTI_DEFTEST_PLANT "$BIN" --gtest_filter="$filter" --gtest_brief=1
  then
    printf 'check-gates: %s failed on the real tree\n' "$filter" >&2
    return 1
  fi
  return 0
}

red() {
  filter=$1
  plant=$2
  if GHOTI_DEFTEST_PLANT="$plant" "$BIN" --gtest_filter="$filter" --gtest_brief=1
  then
    printf 'check-gates: planted %s passed\n' "$plant" >&2
    return 1
  fi
  printf 'check-gates: planted %s turned the gate red\n' "$plant"
  return 0
}

pass 'Site.CrossTenant'
red 'Site.CrossTenant' drop-tenant
pass 'Site.Bypass'
red 'Site.Bypass' skip
pass 'Site.Restore'
red 'Site.Restore' ledger
pass 'Site.RoundTrip'
red 'Site.RoundTrip' lose
pass 'Harness.Lint'
red 'Harness.Lint' unmark
pass 'Site.Sweep'
red 'Site.Sweep' open
pass 'Site.Scanner'
red 'Site.Scanner' plant-id
pass 'Site.MenuSweep'
red 'Site.MenuSweep' menu-open
pass 'Site.LayoutSweep'
red 'Site.LayoutSweep' layout-open
pass 'Site.SettingsSweep'
red 'Site.SettingsSweep' settings-open
