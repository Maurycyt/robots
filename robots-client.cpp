#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "utils.h"
#include "options.h"
#include "communication.h"

namespace {
	boost::asio::io_context clientContext;
	boost::asio::ip::tcp::resolver tcpResolver(clientContext);
	boost::asio::ip::udp::resolver udpResolver(clientContext);
	boost::asio::ip::tcp::endpoint serverEndpoint;
	boost::asio::ip::udp::endpoint GUIEndpoint;

	boost::program_options::variables_map options;

	void handleOptions(int argc, char * * argv) {
		/* Parse options. Check argument validity. */
		try {
			options = parseOptions(argc, argv, getClientOptionsDescription());
		} catch (std::exception & e) {
			throw std::string("Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) + " --help for usage.\n");
		}

		/* Print help message if requested. */
		if (options.count("help")) {
			std::cout << getClientOptionsDescription();
			throw int(0);
		}

		/* Finalize parsing, prepare argument values, including checking the existence of required arguments. */
		try {
			notifyOptions(options);
		} catch (std::exception & e) {
			throw std::string("Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) + " --help for usage.\n");
		}
	}
} // anonymous namespace

int main(int argc, char ** argv) {
	try {
		handleOptions(argc, argv);
	} catch (int & e) {
		return e;
	} catch (std::string & e) {
		std::cerr << e;
		return 1;
	} catch (std::exception & e) {
		std::cerr << e.what();
		return 1;
	}

	/* Resolve addresses. */
	try {
		auto [serverHostStr, serverPortStr] = extractHostAndPort(options["server-address"].as<std::string>());
		serverEndpoint = *tcpResolver.resolve(serverHostStr, serverPortStr);
		auto [GUIHostStr, GUIPortStr] = extractHostAndPort(options["gui-address"].as<std::string>());
		GUIEndpoint = *udpResolver.resolve(GUIHostStr, GUIPortStr);
	} catch (std::exception & e) {
		std::cerr << "Error: " << e.what() << "\nRun " << argv[0] << " --help for usage.\n";
		return 1;
	}

	std::cerr << "Server address: " << serverEndpoint
	          << "\nGUI address: " << GUIEndpoint
	          << "\nListening on port " << options["port"].as<port_t>() << "\n";
}