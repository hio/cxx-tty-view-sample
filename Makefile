
OPTIMIZE=-g -O0
CXXFLAGS=-Wall -pedantic-errors $(OPTIMIZE)
LDFLAGS=-ltermcap

.PHONY: all check clean

all: prog

check:
	echo "no $@ codes"

clean:
	rm -f prog prog.o

prog: prog.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
