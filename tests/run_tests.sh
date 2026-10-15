#!/usr/bin/env bash
# Runs every test case in tests/cases on the parsers, saves the full output
# to tests/output/<case>.out and checks it.
#
# Each case has:
#   <case>.expect  one check per line:
#                    text      the output must contain this text
#                    !text     the output must NOT contain this text
#                    re:REGEX  some output line must match this regex
#                    # ...     comment (the expected outcome in words)
# and either
#   <case>.src     the source program given to the parser, with
#   <case>.args    (optional) extra options, e.g. --grammar FILE
# or
#   <case>.cmd     a bash script run from the project root; $BIN is the
#                  parserx executable and $OUT a scratch directory. Used for
#                  generated inputs and for comparing several runs.
#
# Usage (from the project root):  bash tests/run_tests.sh [path-to-parserx]

cd "$(dirname "$0")/.." || exit 1
BIN="${1:-./parserx}"
[ -x "$BIN" ] || BIN="$BIN.exe"
if [ ! -x "$BIN" ]; then
    echo "parserx executable not found; build it first (make, or see README)" >&2
    exit 1
fi
export BIN

mkdir -p tests/output tests/output/scratch
pass=0
fail=0
failed=()
for expect in tests/cases/*.expect; do
    name=$(basename "$expect" .expect)
    out="tests/output/$name.out"
    if [ -f "tests/cases/$name.cmd" ]; then
        OUT="tests/output/scratch/$name" bash "tests/cases/$name.cmd" > "$out" 2>&1
    else
        args=""
        [ -f "tests/cases/$name.args" ] && args=$(tr -d '\r' < "tests/cases/$name.args")
        # shellcheck disable=SC2086
        "$BIN" $args --input "tests/cases/$name.src" > "$out" 2>&1
        echo "EXIT $?" >> "$out"
    fi
    ok=1
    while IFS= read -r check || [ -n "$check" ]; do
        check="${check%$'\r'}"
        [ -z "$check" ] && continue
        case "$check" in
            "#"*) continue ;;
            "!"*)
                if grep -qF -- "${check#!}" "$out"; then
                    echo "  [$name] unexpected: ${check#!}"
                    ok=0
                fi ;;
            "re:"*)
                if ! grep -qE -- "${check#re:}" "$out"; then
                    echo "  [$name] no line matches: ${check#re:}"
                    ok=0
                fi ;;
            *)
                if ! grep -qF -- "$check" "$out"; then
                    echo "  [$name] missing: $check"
                    ok=0
                fi ;;
        esac
    done < "$expect"
    if [ $ok -eq 1 ]; then
        echo "PASS  $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name   (see $out)"
        fail=$((fail + 1))
        failed+=("$name")
    fi
done

echo
echo "Passed $pass of $((pass + fail)) test cases. Outputs saved in tests/output/"
[ $fail -eq 0 ] || { echo "Failed: ${failed[*]}"; exit 1; }
