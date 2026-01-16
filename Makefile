# Compiler settings
CXX = g++
# Added -ltbb to link the Threading Building Blocks library
CXXFLAGS = -std=c++20 -O3 -march=native -pthread -flto -Wall -Wextra -ltbb

CXXFLAGS += -fopenmp

TARGET = corpus_miner

SRCS = main.cpp corpus_miner.cpp signal_handler.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)
	@echo "--------------------------------------------------"
	@echo "Build complete: ./$(TARGET)"
	@echo "Optimization: -O3, Link-Time Optimization (LTO) enabled"
	@echo "--------------------------------------------------"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

debug: CXXFLAGS = -std=c++20 -g -pthread -Wall
debug: clean all

.PHONY: all clean debug
