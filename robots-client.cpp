#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "buffer.h"
#include "messages.h"
#include "options.h"
#include "utils.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::program_options;

namespace {
	io_context clientContext;
	tcp::resolver tcpResolver(clientContext);
	udp::resolver udpResolver(clientContext);
	tcp::endpoint serverEndpoint;
	udp::endpoint GUIEndpoint;

	variables_map options;

	void handleOptions(int argc, char ** argv) {
		/* Parse options. Check argument validity. */
		try {
			options = parseOptions(argc, argv, getClientOptionsDescription());
		} catch (std::exception & e) {
			throw unrecoverableException(
			    "Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) +
			    " --help for usage.\n"
			);
		}

		/* Print help message if requested. */
		if (options.count("help")) {
			throw needHelp();
		}

		/* Finalize parsing, prepare argument values, including checking the
		 * existence of required arguments. */
		try {
			notifyOptions(options);
		} catch (std::exception & e) {
			throw unrecoverableException(
			    "Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) +
			    " --help for usage.\n"
			);
		}
	}

	void resolveAddresses(char ** argv) {
		try {
			auto [serverHostStr, serverPortStr] =
			    extractHostAndPort(options["server-address"].as<std::string>());
			serverEndpoint = *tcpResolver.resolve(serverHostStr, serverPortStr);
			auto [GUIHostStr, GUIPortStr] =
			    extractHostAndPort(options["gui-address"].as<std::string>());
			GUIEndpoint = *udpResolver.resolve(GUIHostStr, GUIPortStr);
		} catch (std::exception & e) {
			throw unrecoverableException(
			    "Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) +
			    " --help for usage.\n"
			);
		}
	}

	void throwInterrupt([[maybe_unused]] int signal) {
		throw unrecoverableException("\nInterrupted.");
	}
} // namespace

int main(int argc, char ** argv) {
	try {
		installSignalHandler(SIGINT, throwInterrupt, SA_RESTART);
		handleOptions(argc, argv);
		resolveAddresses(argv);

		std::cerr << "Server address: " << serverEndpoint
		          << "\nGUI address: " << GUIEndpoint << "\nListening on port "
		          << options["port"].as<port_t>() << "\n";

		// udp::socket UDPSocket(
		// 	clientContext,
		// 	udp::endpoint(
		// 		udp::v6(), options["port"].as<port_t>()
		// 	)
		// );

		// UDPBuffer buffer(UDPSocket, GUIEndpoint);

		// buffer.receive();

		tcp::acceptor TCPAcceptor(
		    clientContext, tcp::endpoint(tcp::v6(), options["port"].as<port_t>())
		);

		tcp::socket TCPSocket = TCPAcceptor.accept();

		TCPBuffer buffer(TCPSocket);

		DataClientMessage message;
		buffer >> message;

		std::cerr << static_cast<int>(message.type) << "\n"
		          << message.name.value.size() << "\n"
		          << message.name.value << "\n";

	} catch (needHelp & e) {
		/* Exception reserved for --help option. */
		std::cout << getClientOptionsDescription();
		return 0;
	} catch (unrecoverableException & e) {
		/* Something went wrong, we know what it is and cannot recover from it. */
		std::cerr << e.what();
		return 1;
	} catch (std::exception & e) {
		/* Something went wrong and we were not prepared. This is bad. */
		std::cerr << e.what();
		return 2;
	}
}