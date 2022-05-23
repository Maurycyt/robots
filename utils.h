#pragma once

#include <boost/asio.hpp>
#include <cstdint>

using port_t = uint16_t;

std::pair<std::string, std::string>
extractHostAndPort(const std::string & address) {
	std::string::size_type colonPosition = address.rfind(':');
	if (colonPosition == std::string::npos) {
		throw std::invalid_argument(
		    "the argument ('" + address +
		    "') is not a valid address. Colon character not found."
		);
	}

	return {address.substr(0, colonPosition), address.substr(colonPosition + 1)};
}

void installSignalHandler(int signal, void (*handler)(int), int flags) {
	struct sigaction action {};
	sigset_t blockMask;

	sigemptyset(&blockMask);
	action.sa_handler = handler;
	action.sa_mask = blockMask;
	action.sa_flags = flags;

	if (sigaction(signal, &action, nullptr)) {
		throw UnrecoverableException("could not install SIGINT handler.");
	}
}

template <typename E, typename R>
requires(
    (std::same_as<E, boost::asio::ip::udp::endpoint> &&
     std::same_as<R, boost::asio::ip::udp::resolver>) ||
    (std::same_as<E, boost::asio::ip::tcp::endpoint> &&
     std::same_as<R, boost::asio::ip::tcp::resolver>)
) E
    resolveAddress(
        R & resolver, const std::string & address,
        const std::string & programName
    ) {
	try {
		auto [addressStr, portStr] = extractHostAndPort(address);
		return *resolver.resolve(addressStr, portStr);
	} catch (std::exception & e) {
		throw UnrecoverableException(
		    "Error: " + std::string(e.what()) + "\nRun " + programName +
		    " --help for usage.\n"
		);
	}
}