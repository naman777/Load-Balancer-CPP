CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

.PHONY: all test clean

all:
	$(MAKE) -C load-balancer
	$(MAKE) -C server
	$(CXX) $(CXXFLAGS) -o client client.cpp

test:
	$(MAKE) -C load-balancer test

clean:
	$(MAKE) -C load-balancer clean
	$(MAKE) -C server clean
	rm -f client
