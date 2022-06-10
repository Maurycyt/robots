#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>

#ifdef NDEBUG
const bool DEBUG = false;
#else
const bool DEBUG = true;
#endif

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
		throw RobotsException("Error: could not install SIGINT handler.");
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
		throw RobotsException(
		    "Error: " + std::string(e.what()) + "\nRun " + programName +
		    " --help for usage.\n"
		);
	}
}

void debug(const std::string & message) {
	if (DEBUG) {
		std::cerr << message;
	}
}

class Random {
	static const uint64_t constant = 48271;
	static const uint64_t modulo = (1ULL << 31) - 1; // 2147483647

	uint64_t seed;

public:
	Random() : seed(static_cast<uint64_t>(
	        std::chrono::system_clock::now().time_since_epoch().count()
	    )) {
	}

	explicit Random(uint64_t newSeed) : seed(newSeed) {
	}

	uint64_t next() {
		return (seed = seed * constant % modulo);
	}
};