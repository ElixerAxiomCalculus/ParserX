# Test 47 (edge): an unreadable input file gives exit code 4
"$BIN" --input tests/cases/does_not_exist.src
echo "EXIT $?"
