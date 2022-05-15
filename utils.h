#pragma once

#include <cstdint>
#include <boost/asio.hpp>

using port_t = uint16_t;

std::pair<std::string, std::string> extractHostAndPort(const std::string & address) {
	std::string::size_type colonPosition = address.rfind(':');
	if (colonPosition == address.npos) {
		throw std::invalid_argument("Colon not found in address");
	}

	return {address.substr(0, colonPosition), address.substr(colonPosition + 1)};
}