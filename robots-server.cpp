#include <iostream>

int main(int argc, char ** argv) {
	std::cout << "The arguments are:\n";
	for (int i = 1; i <= argc; i++) {
		std::cout << argv[i] << " ";
	}
	std::cout << "\n";
}