CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -fPIC

all: stopwatch

stopwatch: main.cpp
	@command -v pkg-config >/dev/null 2>&1 || { \
		echo "pkg-config is required (install pkg-config)"; exit 1; }
	@pkg-config --exists Qt6Widgets || { \
		echo "Qt6 Widgets development files not found (install qt6-base-dev)"; \
		exit 1; }
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags Qt6Widgets) \
		-o $@ $< $(shell pkg-config --libs Qt6Widgets)

clean:
	rm -f stopwatch

.PHONY: all clean
