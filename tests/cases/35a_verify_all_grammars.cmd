# Test 35: --verify on every loadable grammar in the repository (and the zoo)
n=0
for gr in grammar/*.txt grammar/zoo/*.txt tests/grammars/*.txt; do
    [ -f "$gr" ] || continue
    case "$(basename "$gr")" in bad_*|empty.txt|only_comments.txt|no_arrow.txt|lex_bad*|lex_empty_rule.txt) continue ;; esac
    out=$("$BIN" --grammar "$gr" --code "" --trace none --verify 2>&1)
    echo "GRAMMAR $gr"
    echo "$out" | grep -E "^VERIFY"
    n=$((n + 1))
done
echo "CHECKED $n grammars"
