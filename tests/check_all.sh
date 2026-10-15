#!/usr/bin/env bash
# Extra checks run by `make check-all` after the unit and case tests:
#   1. --verify (DeRemer-Pennello == merge LALR, PGM invariants, compressed
#      lookups) on every loadable grammar in the repository and the zoo
#   2. a fixed-seed fuzz smoke run (--fuzz 2000) on v1 and v2 with invariants
#   3. --explain witnesses verified on every grammar
#   4. every zoo grammar classified as its header says
# Usage (from the project root):  bash tests/check_all.sh [path-to-parserx]

cd "$(dirname "$0")/.." || exit 1
BIN="${1:-./parserx}"
[ -x "$BIN" ] || BIN="$BIN.exe"
failed=0

check() {
    if [ "$2" = "ok" ]; then echo "PASS  $1"; else echo "FAIL  $1"; failed=$((failed + 1)); fi
}

skip() {
    case "$(basename "$1")" in
        bad_*|empty.txt|only_comments.txt|no_arrow.txt|lex_bad*|lex_empty_rule.txt) return 0 ;;
    esac
    return 1
}

for gr in grammar/*.txt grammar/zoo/*.txt tests/grammars/*.txt; do
    skip "$gr" && continue
    out=$("$BIN" --grammar "$gr" --code "" --trace none --verify 2>&1)
    status=ok
    echo "$out" | grep -q "FAILED" && status=bad
    [ "$(echo "$out" | grep -c '^VERIFY LALR dp==merge OK')" = "1" ] || status=bad
    check "verify $gr" "$status"
done

for gr in grammar/language.txt grammar/language_v2.txt; do
    out=$("$BIN" --grammar "$gr" --fuzz 2000 --seed 20261015 2>&1)
    status=ok
    echo "$out" | grep -q "^FUZZ INVARIANTS OK" || status=bad
    check "fuzz 2000 $gr" "$status"
done

for gr in grammar/*.txt grammar/zoo/*.txt; do
    out=$("$BIN" --grammar "$gr" --code "" --trace none --explain 2>&1 | grep -E "^EXPLAIN .* state=")
    status=ok
    echo "$out" | grep -q "verified=no" && status=bad
    check "explain $gr" "$status"
done

for gr in grammar/zoo/*.txt; do
    exp=$(grep -m1 "^# expected:" "$gr" | sed 's/^# expected: //' | tr -d '\r')
    got=$("$BIN" --grammar "$gr" --classify --trace none | grep -m1 '^CLASS ' | sed 's/^CLASS //')
    case "$got" in "$exp"*) status=ok ;; *) status=bad ;; esac
    check "class $gr ($exp)" "$status"
done

echo
if [ $failed -eq 0 ]; then echo "check-all: every extra check passed"; else echo "check-all: $failed check(s) failed"; fi
[ $failed -eq 0 ]
