# Detect if we are on macOS
UNAME_S := $(shell uname -s)
OPT ?= -O1
OPENMP ?= -fopenmp

ifeq ($(UNAME_S), Darwin)
	# macOS: Find Homebrew GCC
	BREW_PREFIX := $(shell brew --prefix)
	CXX := $(shell ls $(BREW_PREFIX)/bin/g++-[0-9]* 2>/dev/null | sort -V | tail -n 1)
	
	ifeq ($(CXX),)
		CXX = g++-14
	endif
	
	# Added -fno-stack-check as it sometimes interferes with Mach-O relocations
	CXXFLAGS = -std=c++20 -g $(OPT) -march=native -pthread -Wall -Wextra -fopenmp \
	           -fno-stack-check -I$(BREW_PREFIX)/include

	# -Wl,-ld_classic: Uses the older, more stable linker that handles GCC better
	# -Wl,-no_compact_unwind: Fixes issues with OpenMP/Exception handling relocations
	LDFLAGS = -L$(BREW_PREFIX)/lib -ltbb $(OPENMP) -Wl,-ld_classic -Wl,-no_compact_unwind
else
	# Linux/Standard settings - added -g for debug symbols
	CXX = g++
	CXXFLAGS = -std=c++20 -g $(OPT) -march=native -pthread -Wall -Wextra -fopenmp
	LDFLAGS = -ltbb $(OPENMP)
endif

TARGET = corpus_miner
SRCS = main.cpp corpus_miner.cpp _ours/bloom_gram_miner.cpp bide/bide_miner.cpp signal_handler.cpp # Added bide_miner.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean clean-reports clean-all

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	@echo "--------------------------------------------------"
	@echo "Build complete: ./$(TARGET)"
	@echo "--------------------------------------------------"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) corpus_data.bin

clean-reports:
	rm -f results_max.csv results_tree.csv visualization.html

clean-all: clean clean-reports
