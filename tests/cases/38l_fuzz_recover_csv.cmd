# Test 38: --recover adds the number of reported errors per parser to the CSV
mkdir -p "$OUT"
"$BIN" --fuzz 50 --recover --csv "$OUT/r.csv" | grep -E "^FUZZ (INVARIANTS|AGREEMENT)"
head -1 "$OUT/r.csv"
