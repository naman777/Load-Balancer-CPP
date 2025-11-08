CXX      = g++
CXXFLAGS = -std=c++17 -pthread -Wall -Wextra -O2
TARGET   = load_balancer

all: $(TARGET)

$(TARGET): main.o LoadBalancer.o
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.o LoadBalancer.o

main.o: main.cpp LoadBalancer.h
	$(CXX) $(CXXFLAGS) -c main.cpp

LoadBalancer.o: LoadBalancer.cpp LoadBalancer.h
	$(CXX) $(CXXFLAGS) -c LoadBalancer.cpp

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
