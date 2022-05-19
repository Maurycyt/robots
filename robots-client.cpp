#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "utils.h"
#include "options.h"
#include "buffer.h"
#include "messages.h"

namespace {
	using namespace boost::asio;
	using namespace boost::asio::ip;
	using namespace boost::program_options;

	io_context clientContext;
	tcp::resolver tcpResolver(clientContext);
	udp::resolver udpResolver(clientContext);
	tcp::endpoint serverEndpoint;
	udp::endpoint GUIEndpoint;

	variables_map options;

	void handleOptions(int argc, char * * argv) {
		/* Parse options. Check argument validity. */
		try {
			options = parseOptions(argc, argv, getClientOptionsDescription());
		} catch (std::exception & e) {
			throw std::string("Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) + " --help for usage.\n");
		}

		/* Print help message if requested. */
		if (options.count("help")) {
			throw needHelp();
		}

		/* Finalize parsing, prepare argument values, including checking the existence of required arguments. */
		try {
			notifyOptions(options);
		} catch (std::exception & e) {
			throw std::string("Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) + " --help for usage.\n");
		}
	}

	void resolveAddresses(char * * argv) {
		try {
		auto [serverHostStr, serverPortStr] = extractHostAndPort(options["server-address"].as<std::string>());
		serverEndpoint = *tcpResolver.resolve(serverHostStr, serverPortStr);
		auto [GUIHostStr, GUIPortStr] = extractHostAndPort(options["gui-address"].as<std::string>());
		GUIEndpoint = *udpResolver.resolve(GUIHostStr, GUIPortStr);
		} catch (std::exception & e) {
			throw std::string("Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) + " --help for usage.\n");
		}
	}

	void throwInterrupt([[maybe_unused]] int signal) {
		throw std::string("\nInterrupted.");
	}
} // anonymous namespace

int main(int argc, char * * argv) {
	try {
		installSignalHandler(SIGINT, throwInterrupt, SA_RESTART);
		handleOptions(argc, argv);
		resolveAddresses(argv);
		

		std::cerr << "Server address: " << serverEndpoint
							<< "\nGUI address: " << GUIEndpoint
							<< "\nListening on port " << options["port"].as<port_t>() << "\n";

		// udp::socket UDPsocket(
		// 	clientContext,
		// 	udp::endpoint(
		// 		udp::v6(), options["port"].as<port_t>()
		// 	)
		// );

		// UDPBuffer buffer(UDPsocket);

		// buffer.receive();

		tcp::acceptor TCPacceptor(
			clientContext,
			tcp::endpoint(
				tcp::v6(), options["port"].as<port_t>()
			)
		);

		tcp::socket TCPsocket = TCPacceptor.accept();

		TCPBuffer buffer(TCPsocket);

		DataClientMessage message;
		message.parse(buffer);

		std::cerr << static_cast<int>(message.type) << "\n" << message.name.data.size() << "\n"
							<< message.name.data << "\n";

	} catch (needHelp & e) {
		/* Exception reserved for --help option. */
		std::cout << getClientOptionsDescription();
		return 0;
	} catch (std::string & e) {
		/* Something went wrong and we know what it is and cannot recover from it. */
		std::cerr << e;
		return 1;
	} catch (std::exception & e) {
		/* Something went wrong and we were not prepared. This is bad. */
		std::cerr << e.what();
		return 1;
	}
}