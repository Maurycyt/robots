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