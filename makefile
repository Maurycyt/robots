CXX = g++
CXX_FLAGS = -g -std=gnu++20 -Wall -Wextra -Wconversion -Wshadow -Werror -O2 -L/usr/lib/x86_64-linux-gnu/ -lboost_program_options

.PHONY: all, clean

all: robots-client robots-server

robots-client: robots-client.o
	$(CXX) $(CXX_FLAGS) $< -o $@

robots-server: robots-server.o
	$(CXX) $(CXX_FLAGS) $< -o $@

robots-client.o: robots-client.cpp options.h
	$(CXX) -c $(CXX_FLAGS) $< -o $@

robots-server.o: robots-server.cpp options.h
	$(CXX) -c $(CXX_FLAGS) $< -o $@

clean:
	rm -f robots-client robots-server *.o