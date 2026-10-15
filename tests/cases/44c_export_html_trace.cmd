# Test 44: --html-trace writes one self-contained page with every parser's steps
mkdir -p "$OUT"
"$BIN" --grammar grammar/language_v2.txt --code "if ( a < b ) x = 1 ; else x = 2 ;" --trace none --html-trace "$OUT/t.html" > /dev/null
head -1 "$OUT/t.html"
grep -c "<script src" "$OUT/t.html" | sed 's/^/EXTERNAL SCRIPTS /'
grep -c "<link" "$OUT/t.html" | sed 's/^/EXTERNAL STYLES /'
grep -o '"name":"[A-Z]*","accepted":true' "$OUT/t.html"
grep -F -o 'if ( id relop id ) id = num ; else id = num ; $' "$OUT/t.html" | head -1
