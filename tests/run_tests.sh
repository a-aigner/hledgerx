#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BIN="$ROOT_DIR/hledgerx"

assert_contains() {
    haystack=$1
    needle=$2
    if ! printf '%s\n' "$haystack" | grep -F -- "$needle" >/dev/null 2>&1; then
        echo "ASSERTION FAILED: expected to find '$needle'"
        exit 1
    fi
}

assert_regex() {
    haystack=$1
    pattern=$2
    if ! printf '%s\n' "$haystack" | grep -E -- "$pattern" >/dev/null 2>&1; then
        echo "ASSERTION FAILED: expected pattern '$pattern'"
        exit 1
    fi
}

TMPDIR=$(mktemp -d "/tmp/hledgerx-tests.XXXXXX")
trap 'rm -rf "$TMPDIR"' EXIT

mkdir -p "$TMPDIR/sub"
cat > "$TMPDIR/main.journal" <<'JOURNAL'
include sub/cash.journal
include "sub/invest.journal"

2026-03-11 Dinner
    expenses:food          30.00 EUR
    assets:cash

2026-07-01 Bonus
    assets:cash             1.00 EUR
    income:salary
JOURNAL

cat > "$TMPDIR/sub/cash.journal" <<'JOURNAL'
2026-03-10 Coffee
    expenses:food           3.50 EUR
    assets:cash
JOURNAL

cat > "$TMPDIR/sub/invest.journal" <<'JOURNAL'
2026-03-12 Buy ETF
    assets:broker:etf       2.00 VTI
    assets:cash
JOURNAL

out=$($BIN balance "$TMPDIR/main.journal")
assert_contains "$out" "assets:broker:etf"
assert_contains "$out" "total"
assert_regex "$out" 'total[[:space:]]+0\.00 EUR$'
assert_regex "$out" 'total[[:space:]]+0\.00 VTI$'

out=$($BIN bal "$TMPDIR/main.journal" --tree)
assert_regex "$out" '^assets[[:space:]]+-32\.50 EUR$'
assert_regex "$out" '^  cash[[:space:]]+-32\.50 EUR$'
assert_regex "$out" 'total[[:space:]]+0\.00 EUR$'

out=$($BIN reg -f "$TMPDIR/main.journal" assets: -b 2026-03-10 -e 2026-03-12 -d 2)
assert_contains "$out" "assets:broker"
assert_contains "$out" "assets:cash"

out=$($BIN balance "$TMPDIR/main.journal" --monthly)
assert_contains "$out" "period 2026-03"
assert_contains "$out" "total"

out=$($BIN bal "$TMPDIR/main.journal" -M)
assert_contains "$out" "period 2026-03"
assert_contains "$out" "period 2026-07"

out=$($BIN balance "$TMPDIR/main.journal" --quarterly)
assert_contains "$out" "period 2026-Q1"
assert_contains "$out" "period 2026-Q3"

out=$($BIN bal "$TMPDIR/main.journal" -Q)
assert_contains "$out" "period 2026-Q1"

out=$($BIN register "$TMPDIR/main.journal" --yearly)
assert_contains "$out" "period 2026"

out=$($BIN reg "$TMPDIR/main.journal" -Y)
assert_contains "$out" "period 2026"

out=$($BIN register "$TMPDIR/main.journal" --daily)
assert_contains "$out" "period 2026-03-10"
assert_contains "$out" "period 2026-03-11"
assert_contains "$out" "period 2026-03-12"

out=$($BIN reg "$TMPDIR/main.journal" -D)
assert_contains "$out" "period 2026-03-10"
assert_contains "$out" "period 2026-07-01"

out=$($BIN balance "$TMPDIR/main.journal" --output-format csv)
assert_contains "$out" "account,amount,commodity,row_type"
assert_contains "$out" "assets:cash"
assert_contains "$out" ",total"

out=$($BIN register "$TMPDIR/main.journal" --daily --csv)
assert_contains "$out" "period,date,description,account,change,balance,commodity"
assert_contains "$out" "2026-03-10,2026-03-10,Coffee"
assert_contains "$out" "2026-07-01,2026-07-01,Bonus"

out=$($BIN acct "$TMPDIR/main.journal" --tree)
assert_contains "$out" "assets"
assert_contains "$out" "  cash"

if $BIN print "$TMPDIR/main.journal" --monthly >"$TMPDIR/period_invalid.out" 2>&1; then
    echo "ASSERTION FAILED: --monthly should not be accepted by print"
    exit 1
fi
period_invalid_err=$(cat "$TMPDIR/period_invalid.out")
assert_contains "$period_invalid_err" "--daily/--monthly/--quarterly/--yearly is only supported by 'balance' and 'register'"

if $BIN accounts "$TMPDIR/main.journal" --csv >"$TMPDIR/csv_invalid.out" 2>&1; then
    echo "ASSERTION FAILED: --csv should not be accepted by accounts"
    exit 1
fi
csv_invalid_err=$(cat "$TMPDIR/csv_invalid.out")
assert_contains "$csv_invalid_err" "--output-format csv is only supported by 'balance' and 'register'"

cat > "$TMPDIR/bad.journal" <<'JOURNAL'
not_a_directive 123
JOURNAL
if $BIN balance "$TMPDIR/bad.journal" --strict >"$TMPDIR/strict.out" 2>&1; then
    echo "ASSERTION FAILED: strict mode should reject invalid directive"
    exit 1
fi
strict_err=$(cat "$TMPDIR/strict.out")
assert_contains "$strict_err" "Load failed:"
assert_regex "$strict_err" 'bad\.journal:1:'

cat > "$TMPDIR/unbalanced.journal" <<'JOURNAL'
2026-03-20 Broken
    assets:cash           10.00 EUR
    income:salary         -8.00 EUR
JOURNAL
if $BIN balance "$TMPDIR/unbalanced.journal" --strict >"$TMPDIR/unbalanced_strict.out" 2>&1; then
    echo "ASSERTION FAILED: strict mode should reject unbalanced transactions"
    exit 1
fi
unbalanced_strict_err=$(cat "$TMPDIR/unbalanced_strict.out")
assert_regex "$unbalanced_strict_err" 'unbalanced\.journal:1:'
assert_contains "$unbalanced_strict_err" "Unbalanced transaction"

out=$($BIN balance "$TMPDIR/unbalanced.journal")
assert_contains "$out" "assets:cash"

cat > "$TMPDIR/one-posting.journal" <<'JOURNAL'
2026-03-21 Invalid
    assets:cash           10.00 EUR
JOURNAL
if $BIN balance "$TMPDIR/one-posting.journal" --strict >"$TMPDIR/one_posting_strict.out" 2>&1; then
    echo "ASSERTION FAILED: strict mode should reject one-posting transactions"
    exit 1
fi
one_posting_err=$(cat "$TMPDIR/one_posting_strict.out")
assert_regex "$one_posting_err" 'one-posting\.journal:1:'
assert_contains "$one_posting_err" "at least two postings"

cat > "$TMPDIR/missing-include.journal" <<'JOURNAL'
include sub/does-not-exist.journal
JOURNAL
if $BIN balance "$TMPDIR/missing-include.journal" >"$TMPDIR/missing.out" 2>&1; then
    echo "ASSERTION FAILED: missing include should fail"
    exit 1
fi
missing_err=$(cat "$TMPDIR/missing.out")
assert_regex "$missing_err" 'missing-include\.journal:1:'
assert_contains "$missing_err" "Included file not found"

mkdir -p "$TMPDIR/cycle"
cat > "$TMPDIR/cycle/a.journal" <<'JOURNAL'
include b.journal
JOURNAL
cat > "$TMPDIR/cycle/b.journal" <<'JOURNAL'
include a.journal
JOURNAL
if $BIN balance "$TMPDIR/cycle/a.journal" >"$TMPDIR/cycle.out" 2>&1; then
    echo "ASSERTION FAILED: include cycle should fail"
    exit 1
fi
cycle_err=$(cat "$TMPDIR/cycle.out")
assert_contains "$cycle_err" "Include cycle detected"

echo "All tests passed"
