# Test 32 (edge): a generated 1 MB v2 program is lexed, parsed and checked
mkdir -p "$OUT"
awk 'BEGIN { printf "int x ;\n"; for (i = 0; i < 87382; i++) printf "x = x + 1 ;\n" }' > "$OUT/big.src"
wc -c < "$OUT/big.src" | tr -d ' ' | sed 's/^/BYTES /'
"$BIN" --grammar grammar/language_v2.txt --input "$OUT/big.src" --trace none
echo "EXIT $?"
