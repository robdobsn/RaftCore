# Compiler
CC = g++

# Compiler flags
CFLAGS = -Wall -std=c++20 -lc -g -DRAFT_CORE

# Include paths
INCLUDES = -I../unit_tests/main -I../components/core/ArduinoUtils -I../components/core/RaftJson -I.

# Source files
SOURCES = main.cpp utils.cpp ../components/core/ArduinoUtils/ArduinoWString.cpp

# Output binary
OUTPUT = linux_unit_tests

all: $(OUTPUT)

$(OUTPUT): $(SOURCES) ../components/core/RaftJson/RaftJson.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(OUTPUT) $(SOURCES)

clean:
	rm -f $(OUTPUT)
