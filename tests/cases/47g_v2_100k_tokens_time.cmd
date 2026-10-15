# Test 47: a generated v2 program of about 10^5 tokens is lexed, parsed by all four
# parsers and checked within a 20 second budget on this machine
mkdir -p "$OUT"
awk 'BEGIN { printf "int x ;\nint a [ 10 ] ;\n"; for (i = 0; i < 6250; i++) printf "if ( x < 10 ) { a [ x ] = x * 2 + 1 ; x += 1 ; } else x = 0 ;\n" }' > "$OUT/big.src"
start=$(date +%s)
"$BIN" --grammar grammar/language_v2.txt --input "$OUT/big.src" --trace none | grep -E "^(Tokens|RESULT)"
end=$(date +%s)
[ $((end - start)) -le 20 ] && echo "TIME OK" || echo "TIME OVER BUDGET $((end - start)) s"
