# Test 41: every zoo grammar is classified as its header says (# expected: ...)
n=0
for gr in grammar/zoo/*.txt; do
    exp=$(grep -m1 "^# expected:" "$gr" | sed 's/^# expected: //' | tr -d '\r')
    got=$("$BIN" --grammar "$gr" --classify --trace none | grep -m1 '^CLASS ' | sed 's/^CLASS //')
    case "$got" in
        "$exp"*) echo "MATCH $gr: $got" ;;
        *) echo "MISMATCH $gr: expected [$exp] got [$got]" ;;
    esac
    n=$((n + 1))
done
echo "ZOO $n grammars"
