CXX      = g++
CXXFLAGS = -std=c++17 -pthread -Wall -Wextra -O2
TARGET   = load_balancer
TEST_BIN = ../tests/test_lb

all: $(TARGET)

$(TARGET): main.o LoadBalancer.o
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.o LoadBalancer.o

HEADERS = LoadBalancer.h backend_selector.h logger.h config.h

main.o: main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c main.cpp

LoadBalancer.o: LoadBalancer.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c LoadBalancer.cpp

$(TEST_BIN): ../tests/test_lb.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TEST_BIN) ../tests/test_lb.cpp LoadBalancer.o

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -f $(TARGET) *.o $(TEST_BIN)

.PHONY: all test clean
