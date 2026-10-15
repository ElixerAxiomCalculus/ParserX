# Test 38: same seed gives a byte-identical CSV; a different seed gives a different one
mkdir -p "$OUT"
"$BIN" --fuzz 100 --seed 7 --csv "$OUT/a.csv" > /dev/null
"$BIN" --fuzz 100 --seed 7 --csv "$OUT/b.csv" > /dev/null
"$BIN" --fuzz 100 --seed 8 --csv "$OUT/c.csv" > /dev/null
cmp -s "$OUT/a.csv" "$OUT/b.csv" && echo "SAME SEED IDENTICAL" || echo "SAME SEED DIFFERENT"
cmp -s "$OUT/a.csv" "$OUT/c.csv" && echo "OTHER SEED IDENTICAL" || echo "OTHER SEED DIFFERENT"
head -1 "$OUT/a.csv"
wc -l < "$OUT/a.csv" | tr -d ' ' | sed 's/^/LINES /'
