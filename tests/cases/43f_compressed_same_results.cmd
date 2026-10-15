# Test 43: every .src case gives the same acceptance and error token with and without --compressed
n=0
diff=0
for src in tests/cases/*.src; do
    name=$(basename "$src" .src)
    args=""
    [ -f "tests/cases/$name.args" ] && args=$(tr -d '\r' < "tests/cases/$name.args")
    # shellcheck disable=SC2086
    a=$("$BIN" $args --trace none --input "$src" 2>&1 | grep -E "^RESULT (SLR|LALR|CLR|PGM) " | sed 's/ step=.*//')
    # shellcheck disable=SC2086
    b=$("$BIN" $args --trace none --compressed --input "$src" 2>&1 | grep -E "^RESULT (SLR|LALR|CLR|PGM) " | sed 's/ step=.*//')
    if [ "$a" != "$b" ]; then
        echo "DIFFERENT $name"
        diff=$((diff + 1))
    fi
    n=$((n + 1))
done
echo "COMPARED $n cases, $diff different"
