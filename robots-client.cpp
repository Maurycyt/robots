#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "utils.h"
#include "options.h"
#include "communication.h"

int main(int argc, char ** argv) {
	boost::asio::io_context clientContext;
	boost::asio::ip::tcp::resolver tcpResolver(clientContext);
	boost::asio::ip::udp::resolver udpResolver(clientContext);
	boost::asio::ip::tcp::endpoint serverEndpoint;
	boost::asio::ip::udp::endpoint GUIEndpoint;

	/* Parse options and resolve addresses. */
	boost::program_options::variables_map options = parseOptions(argc, argv, getClientOptionsDescription());

	try {
		auto [serverHostStr, serverPortStr] = extractHostAndPort(options["server-address"].as<std::string>());
		serverEndpoint = *tcpResolver.resolve(serverHostStr, serverPortStr);
		auto [GUIHostStr, GUIPortStr] = extractHostAndPort(options["gui-address"].as<std::string>());
		GUIEndpoint = *udpResolver.resolve(GUIHostStr, GUIPortStr);
	} catch (std::exception & e) {
		std::cerr << "Error: " << e.what() << "\nRun " << argv[0] << " --help for usage.\n";
		exit(1);
	}

	std::cerr << "Server address: " << serverEndpoint
	          << "\nGUI address: " << GUIEndpoint
	          << "\nListening on port " << options["port"].as<port_t>() << "\n";
}