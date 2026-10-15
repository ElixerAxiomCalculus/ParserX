# Test 47: 10000 statements with every parser including PGM
"$BIN" --parser pgm --trace none --input tests/cases/12b_many_statements.src
echo "EXIT $?"
