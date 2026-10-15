# Token specification for the v2 language (grammar/language_v2.txt uses it
# through %lexer). Longest match wins; on equal length the earlier rule wins.
# Keywords are identifiers that become their own token type.
%skip     [ \t\r\n]+
%skip     //[^\n]*
real      [0-9]+\.[0-9]+
num       [0-9]+
%keywords if else while for int float print
id        [A-Za-z_][A-Za-z0-9_]*
relop     <=|>=|==|!=|<|>
%literals && || += -= ! = ; ( ) { } [ ] + - * / % &
