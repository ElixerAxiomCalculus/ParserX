# Test 47: 5000 levels of nesting with every parser including PGM
"$BIN" --parser pgm --trace none --input tests/cases/12a_deep_nesting.src
echo "EXIT $?"
