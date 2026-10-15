# Test 44: --dot automaton writes one Graphviz file per parser; conflict states are red
mkdir -p "$OUT"
"$BIN" --code "x = 1 ;" --trace none --dot automaton "$OUT/a.dot" | grep "^EXPORT" | sed "s#$OUT/##"
head -1 "$OUT/a_slr.dot"
grep -c " -> " "$OUT/a_slr.dot" | sed 's/^/SLR EDGES /'
grep "s33 \[" "$OUT/a_lalr.dot" | grep -o "color=red, penwidth=3"
grep -c "^  s[0-9]* \[label" "$OUT/a_clr.dot" | sed 's/^/CLR NODES /'
