CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET   = echo_server

all: $(TARGET)

$(TARGET): server.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) server.cpp

clean:
	rm -f $(TARGET)

.PHONY: all clean
