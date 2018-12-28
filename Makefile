ifeq ($(UNWIND_SRC),)
UNWIND_INCLUDES =
UNWIND_LIBS = -lunwind
else
UNWIND_INCLUDES = -I$(UNWIND_SRC)/include
UNWIND_LIBS = -Wl,--whole-archive $(UNWIND_SRC)/src/.libs/libunwind.a -Wl,--no-whole-archive
endif

CXX ?= g++
CC ?= gcc
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden -std=gnu++11 $(UNWIND_INCLUDES)
CFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden -std=c99 $(UNWIND_INCLUDES)

all: libmemtrail.so sample-c sample-cpp

libmemtrail.so: memtrail.cpp memtrail.version
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC $(UNWIND_LIBS) -Wl,--version-script,memtrail.version -o $@ $<  -ldl

%: %.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

%: %.c
	$(CC) -O0 -g2 -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping https://raw.githubusercontent.com/jrfonseca/gprof2dot/master/gprof2dot.py
	chmod +x gprof2dot.py

sample-c: sample-c.c
sample-cpp: sample-cpp.cpp memtrail.h

test: libmemtrail.so sample gprof2dot.py
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	./memtrail record ./sample
	./memtrail dump
	./memtrail report --show-snapshots --show-snapshot-deltas --show-cumulative-snapshot-delta --show-maximum --show-leaks --output-graphs
	$(foreach LABEL, snapshot-0 snapshot-1 snapshot-1-delta maximum leaked, ./gprof2dot.py -f json memtrail.$(LABEL).json > memtrail.$$LABEL.dot ;)

test-debug: libmemtrail.so sample
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	./memtrail record --debug ./sample

bench: libmemtrail.so benchmark
	$(RM) memtrail.data
	./memtrail record ./benchmark
	time -p ./memtrail report --show-maximum

profile: benchmark gprof2dot.py
	./memtrail record ./benchmark
	python -m cProfile -o memtrail.pstats -- ./memtrail report --show-maximum
	./gprof2dot.py -f pstats memtrail.pstats > memtrail.dot

clean:
	$(RM) libmemtrail.so gprof2dot.py sample-c sample-cpp


.PHONY: all test test-debug bench profile clean
