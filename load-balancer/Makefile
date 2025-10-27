CXX = g++
CXXFLAGS = -std=c++11 -pthread
TARGET = load_balancer

all: $(TARGET)

$(TARGET): main.o LoadBalancer.o
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.o LoadBalancer.o

main.o: main.cpp LoadBalancer.h
	$(CXX) $(CXXFLAGS) -c main.cpp

LoadBalancer.o: LoadBalancer.cpp LoadBalancer.h
	$(CXX) $(CXXFLAGS) -c LoadBalancer.cpp

clean:
	rm -f $(TARGET) *.o
