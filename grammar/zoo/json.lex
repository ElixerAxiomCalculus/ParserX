# Tokens for zoo/json.txt
%skip     [ \t\r\n]+
string    "([^"\\]|\\.)*"
number    -?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?
%keywords true false null
word      [a-z]+
%literals { } [ ] , :
