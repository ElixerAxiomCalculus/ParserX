# Test 41: the random grammar CSV is reproducible for a fixed seed
mkdir -p "$OUT"
"$BIN" --random-grammars 100 --seed 3 --csv "$OUT/a.csv" > /dev/null
"$BIN" --random-grammars 100 --seed 3 --csv "$OUT/b.csv" > /dev/null
cmp -s "$OUT/a.csv" "$OUT/b.csv" && echo "IDENTICAL" || echo "DIFFERENT"
wc -l < "$OUT/a.csv" | tr -d ' ' | sed 's/^/LINES /'
