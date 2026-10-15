# Test 44: --csv-tables writes ACTION and GOTO tables; terminals that contain a comma are quoted
mkdir -p "$OUT"
"$BIN" --grammar grammar/zoo/json.txt --code "[ 1 ]" --trace none --parser clr --csv-tables "$OUT/csv" | grep "^EXPORT" | sed "s#$OUT/##"
head -1 "$OUT/csv/clr_action.csv"
head -1 "$OUT/csv/clr_goto.csv"
wc -l < "$OUT/csv/clr_action.csv" | tr -d ' ' | sed 's/^/LINES /'
