#pragma once

#include <cstdint>
#include <boost/asio.hpp>

using port_t = uint16_t;

std::pair<std::string, std::string> extractHostAndPort(const std::string & address) {
	std::string::size_type colonPosition = address.rfind(':');
	if (colonPosition == address.npos) {
		throw std::invalid_argument("the argument ('" + address + "') is not a valid address. Colon character not found.");
	}

	return {address.substr(0, colonPosition), address.substr(colonPosition + 1)};
}

void installSignalHandler(int signal, void (*handler)(int), int flags) {
	struct sigaction action;
	sigset_t blockMask;

	sigemptyset(&blockMask);
	action.sa_handler = handler;
	action.sa_mask = blockMask;
	action.sa_flags = flags;

	if(sigaction(signal, &action, NULL)) {
		throw std::string("could not install SIGINT handler.");
	}
}