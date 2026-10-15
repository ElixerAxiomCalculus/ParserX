# Builds the ParserX command line program (parserx.cpp + src/), the original
# fixed demo (src/main.cpp) and the unit tests, and runs the test suites.
#
#   make            build parserx and the demo
#   make test       build parserx and the unit tests, run both test suites
#   make unit       build and run the unit tests only
#   make check-all  -Werror build, unit tests, case tests, --verify on every
#                   grammar, a fixed-seed fuzz smoke run and the invariants
#   make sanitize   (Linux) AddressSanitizer + UBSan build of parserx and the
#                   case tests; MinGW does not support ASan
#   make clean      remove the programs and saved test output
#
# On Windows with MSYS2 use mingw32-make instead of make.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

# Link the runtime statically on Windows so the program does not pick up
# an incompatible libstdc++ DLL from another MinGW on the PATH, and give the
# main thread a 64 MB stack for deeply nested programs.
ifeq ($(OS),Windows_NT)
LDFLAGS  += -static -Wl,--stack,67108864
EXE       = .exe
endif

CORE_SRCS = src/grammar.cpp src/lexer.cpp src/first_follow.cpp src/slr.cpp \
            src/clr.cpp src/lalr.cpp src/table.cpp src/parser.cpp src/comparison.cpp \
            src/regex.cpp src/lexgen.cpp src/lalr_dp.cpp src/pgm.cpp src/compress.cpp \
            src/context.cpp src/fuzz.cpp src/explain.cpp src/precision.cpp \
            src/classify.cpp src/randgram.cpp src/ast.cpp src/sema.cpp src/interp.cpp \
            src/tac.cpp src/glr.cpp src/visual.cpp src/bench.cpp
HEADERS   = $(wildcard src/*.h)

.PHONY: all test unit check-all sanitize clean

all: parserx parser

parserx: parserx.cpp $(CORE_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -Isrc parserx.cpp $(CORE_SRCS) -o $@ $(LDFLAGS)

parser: src/main.cpp $(CORE_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) src/main.cpp $(CORE_SRCS) -o $@ $(LDFLAGS)

unit_tests: tests/unit/unit_tests.cpp $(CORE_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -Isrc tests/unit/unit_tests.cpp $(CORE_SRCS) -o $@ $(LDFLAGS)

unit: unit_tests
	./unit_tests$(EXE)

test: parserx unit
	bash tests/run_tests.sh

check-all:
	$(MAKE) clean
	$(MAKE) parserx unit_tests CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Werror"
	./unit_tests$(EXE)
	bash tests/run_tests.sh
	bash tests/check_all.sh

sanitize:
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc parserx.cpp \
	    $(CORE_SRCS) -o parserx
	bash tests/run_tests.sh

clean:
	rm -f parserx parserx.exe parser parser.exe unit_tests unit_tests.exe
	rm -rf tests/output
