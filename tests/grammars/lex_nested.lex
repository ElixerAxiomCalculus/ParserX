# Nested groups, classes and alternation
%skip  [ \t\r\n]+
kw     abc
hex    0x([0-9a-f]|[A-F])+
chain  (ab|(cd)+)*e
name   [a-z]+
