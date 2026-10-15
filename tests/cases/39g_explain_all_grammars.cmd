# Test 39: every unresolved conflict of every grammar in the repository gets a verified witness
n=0
for gr in grammar/*.txt grammar/zoo/*.txt tests/grammars/*.txt; do
    [ -f "$gr" ] || continue
    case "$(basename "$gr")" in bad_*|empty.txt|only_comments.txt|no_arrow.txt|lex_bad*|lex_empty_rule.txt) continue ;; esac
    "$BIN" --grammar "$gr" --code "" --explain --trace none 2>&1 | grep -E "^EXPLAIN .* state=" | sed "s#^#$gr: #"
    n=$((n + 1))
done
echo "CHECKED $n grammars"
