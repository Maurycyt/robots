#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <queue>
#include <semaphore>
#include <thread>

#include "buffer.h"
#include "messages.h"
#include "options.h"
#include "utils.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::program_options;

namespace {
	variables_map handleOptions(int argc, char ** argv) {
		variables_map options;
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

		return options;
	}

	void throwInterrupt([[maybe_unused]] int signal) {
		throw unrecoverableException("\nInterrupted.\n");
	}

	class ClientVariables {
	public:
		io_context clientContext{};

		variables_map options;

		udp::resolver UDPResolver{clientContext};
		tcp::resolver TCPResolver{clientContext};
		udp::endpoint GUIEndpoint{};
		tcp::endpoint serverEndpoint{};
		udp::socket GUISocket{clientContext};
		tcp::socket serverSocket{clientContext};

		std::queue<DataInputMessage> inputMessages{};
		std::queue<DataServerMessage> serverMessages{};

		std::counting_semaphore<> messageSemaphore{0};

		ClientVariables(int argc, char ** argv) {
			options = handleOptions(argc, argv);
			GUIEndpoint = resolveAddress<udp::endpoint, udp::resolver>(
			    UDPResolver, options["gui-address"].as<std::string>(),
			    std::string(argv[0])
			);
			serverEndpoint = resolveAddress<tcp::endpoint, tcp::resolver>(
			    TCPResolver, options["server-address"].as<std::string>(),
			    std::string(argv[0])
			);
			GUISocket = udp::socket(
			    clientContext, udp::endpoint(udp::v6(), options["port"].as<port_t>())
			);
			serverSocket.connect(serverEndpoint);
		}
	};

	void listenToGUI(ClientVariables & variables) {
		UDPBuffer GUIBufferIn(variables.GUISocket, variables.GUIEndpoint);
	}

	void listenToServer(ClientVariables & variables) {
		TCPBuffer serverBufferIn(variables.serverSocket);
	}

	[[noreturn]] void mainLoop(ClientVariables & variables) {
		/* If there are messages from both the GUI and the server, then does the
		 * server's message have priority? Toggled with each loop rotation. */
		bool serverPriority = false;
		while (true) {
			serverPriority = !serverPriority;
			variables.messageSemaphore.acquire();
		}
	}
} // namespace

int main(int argc, char ** argv) {
	try {
		/* Install SIGINT handler, prepare options, endpoints, sockets, buffers, and
		 * message queues. Save all to a ClientVariables class. */
		installSignalHandler(SIGINT, throwInterrupt, SA_RESTART);
		ClientVariables variables{argc, argv};

		UDPBuffer GUIBufferOut(variables.GUISocket, variables.GUIEndpoint);
		TCPBuffer serverBufferOut(variables.serverSocket);

		std::thread GUIListener(listenToGUI, std::ref(variables));
		std::thread serverListener(listenToServer, std::ref(variables));

		std::cout << "Server address: " << variables.serverEndpoint
		          << "\nGUI address: " << variables.GUIEndpoint
		          << "\nListening on port "
		          << variables.options["port"].as<port_t>() << "\n";

		/* Main loop. */
		mainLoop(variables);

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