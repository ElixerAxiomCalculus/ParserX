# Test 43: 5000 levels of nesting with compressed tables for all four parsers
"$BIN" --compressed --trace none --input tests/cases/12a_deep_nesting.src
echo "EXIT $?"
