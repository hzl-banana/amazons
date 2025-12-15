# Compiler
CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall

# Targets
TARGET = amazons
SOURCES = amazons.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) -ljsoncpp

# Clean
clean:
	rm -f $(TARGET) $(OBJECTS)

# Run with test input
test: $(TARGET)
	echo '{"requests":[],"responses":[]}' | ./$(TARGET)

.PHONY: all clean test
