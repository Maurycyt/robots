CXX = g++
CXX_FLAGS = -g -std=gnu++20 -Wall -Wextra -Wconversion -Wshadow -Werror -O2
LINKS = -lboost_program_options -pthread
HEADERS = exceptions.h utils.h options.h buffer.h messages.h

.PHONY: all clean format

all: robots-client robots-server

robots-client: robots-client.o
	$(CXX) $(CXX_FLAGS) $< $(LINKS) -o $@

robots-server: robots-server.o
	$(CXX) $(CXX_FLAGS) $< $(LINKS) -o $@

robots-client.o: robots-client.cpp $(HEADERS)
	$(CXX) -c $(CXX_FLAGS) $< $(LINKS) -o $@

robots-server.o: robots-server.cpp $(HEADERS)
	$(CXX) -c $(CXX_FLAGS) $< $(LINKS) -o $@

clean:
	rm -f robots-client robots-server *.o

format:
	clang-format-14 -i -style=file *.h *.cpp 2>/dev/null || echo "\nclang-format-14 or later is required."