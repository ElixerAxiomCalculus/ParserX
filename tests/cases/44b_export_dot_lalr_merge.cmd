# Test 44: --dot lalr-merge colours each CLR state by its LALR state; merged conflict states are red
mkdir -p "$OUT"
"$BIN" --grammar grammar/lalr_vs_clr.txt --code "b c d" --trace none --dot lalr-merge "$OUT/m.dot" > /dev/null
grep -E '^  c(4|7) ' "$OUT/m.dot"
grep -c "LALR" "$OUT/m.dot" | sed 's/^/CLR STATES /'
