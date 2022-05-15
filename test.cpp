#include <cstdio>
#include <string>

int main() {
	std::string name("Żółć!");
	for (char * i = name.data(); *i != 0; i++) {
		printf("%d ", (unsigned char)(*i));
	}
}
