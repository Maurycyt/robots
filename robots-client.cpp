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
			throw UnrecoverableException(
			    "Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) +
			    " --help for usage.\n"
			);
		}

		/* Print help message if requested. */
		if (options.count("help")) {
			throw NeedHelp();
		}

		/* Finalize parsing, prepare argument values, including checking the
		 * existence of required arguments. */
		try {
			notifyOptions(options);
		} catch (std::exception & e) {
			throw UnrecoverableException(
			    "Error: " + std::string(e.what()) + "\nRun " + std::string(argv[0]) +
			    " --help for usage.\n"
			);
		}

		return options;
	}

	void throwInterrupt([[maybe_unused]] int signal) {
		throw UnrecoverableException("\nInterrupted.\n");
	}

	enum class GameState {
		Lobby,
		Game
	};

	class ClientVariables {
	public:
		/* Communication related variables. */
		io_context clientContext{};

		variables_map options;

		udp::resolver UDPResolver{clientContext};
		tcp::resolver TCPResolver{clientContext};
		udp::endpoint GUIEndpoint{};
		tcp::endpoint serverEndpoint{};
		udp::socket GUISocket{clientContext};
		tcp::socket serverSocket{clientContext};

		std::queue<DataInputMessage> inputMessages{};
		std::counting_semaphore<1> inputSemaphore{1};
		std::queue<DataServerMessage> serverMessages{};
		std::counting_semaphore<1> serverSemaphore{1};

		std::counting_semaphore<65536> messageSemaphore{0};

		GameState state{GameState::Lobby};

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

	[[noreturn]] void listenToGUI(ClientVariables & variables) {
		UDPBuffer GUIBufferIn(variables.GUISocket, variables.GUIEndpoint);
		DataInputMessage message;

		while (true) {
			try {
				GUIBufferIn >> message;
			} catch (BadRead & e) {
				continue;
			} catch (BadType & e) {
				continue;
			}
			variables.inputSemaphore.acquire();
			variables.inputMessages.push(message);
			variables.inputSemaphore.release();
			variables.messageSemaphore.release();
		}
	}

	[[noreturn]] void listenToServer(ClientVariables & variables) {
		TCPBuffer serverBufferIn(variables.serverSocket);

		while (true) {
			DataServerMessage message;
			serverBufferIn >> message;
			variables.serverSemaphore.acquire();
			variables.serverMessages.push(message);
			variables.serverSemaphore.release();
			variables.messageSemaphore.release();
		}
	}

	const DataClientMessage & processInputMessage(
	    const ClientVariables & variables, const DataInputMessage & inMessage
	) {
		static DataClientMessage outMessage;
		if (variables.state == GameState::Lobby) {
			std::cerr << "Something received.\n";
			/* If GameStarted message not received, send Join. */
			outMessage.type = ClientMessageEnum::Join;
			outMessage.name.value =
			    variables.options["player-name"].as<std::string>();
		} else {
			/* Otherwise, just forward the message, pretty much. */
			switch (inMessage.type) {
			case InputMessageEnum::PlaceBomb:
				std::cerr << "Place bomb received.\n";
				outMessage.type = ClientMessageEnum::PlaceBomb;
				break;
			case InputMessageEnum::PlaceBlock:
				std::cerr << "Place block received.\n";
				outMessage.type = ClientMessageEnum::PlaceBlock;
				break;
			case InputMessageEnum::Move:
				std::cerr << "Move received.\n";
				outMessage.type = ClientMessageEnum::Move;
				outMessage.direction = inMessage.direction;
				break;
			default:
				break;
			}
		}
		return outMessage;
	}

	const DataDrawMessage & processServerMessage(
	    ClientVariables & variables, const DataServerMessage & inMessage
	) {
		static DataDrawMessage outMessage;

		switch(inMessage.type) {
		case ServerMessageEnum::Hello:
			std::cerr << "Hello received.\n";
			outMessage.serverName = inMessage.serverName;
			outMessage.playerCount = inMessage.playerCount;
			outMessage.sizeX = inMessage.sizeX;
			outMessage.sizeY = inMessage.sizeY;
			outMessage.gameLength = inMessage.gameLength;
			outMessage.explosionRadius = inMessage.explosionRadius;
			outMessage.bombTimer = inMessage.bombTimer;
			break;
		case ServerMessageEnum::AcceptedPlayer:
			std::cerr << "Accepted player received.\n";
			outMessage.players.map.insert({inMessage.playerID, inMessage.player});
			break;
		case ServerMessageEnum::GameStarted:
			std::cerr << "Game started received.\n";
			variables.state = GameState::Game;
			outMessage.type = DrawMessageEnum::Game;
			outMessage.players = inMessage.players;
			break;
		case ServerMessageEnum::Turn:
			std::cerr << "Turn received.\n";
			outMessage.turn = inMessage.turn;
			for (const DataEvent & event : inMessage.events.list) {
				std::cerr << "Reading next event...\n";
				DataBomb bomb;
				switch(event.type) {
				case EventEnum::BombPlaced:
					std::cerr << "A bomb has been placed.\n";
					bomb.position = event.position;
					bomb.timer = inMessage.bombTimer;
					outMessage.bombs.list.push_back(bomb);
					break;
				case EventEnum::BombExploded:
					std::cerr << "A bomb exploded.\n";
					break;
				case EventEnum::PlayerMoved:
					std::cerr << "A player moved.\n";
					outMessage.playerPositions.map[event.playerID] = event.position;
					break;
				case EventEnum::BlockPlaced:
					std::cerr << "A block has been placed.\n";
					outMessage.blocks.list.push_back(event.position);
					break;
				default:
					break;
				}
			}
			break;
		case ServerMessageEnum::GameEnded:
			std::cerr << "Game ended received.\n";
			variables.state = GameState::Lobby;
			outMessage.type = DrawMessageEnum::Lobby;
			outMessage.playerPositions.map.clear();
			outMessage.blocks.list.clear();
			outMessage.bombs.list.clear();
			outMessage.scores = inMessage.scores;
			break;
		default:
			break;
		}

		return outMessage;
	}

	[[noreturn]] void mainLoop(ClientVariables & variables) {
		UDPBuffer GUIBufferOut(variables.GUISocket, variables.GUIEndpoint);
		TCPBuffer serverBufferOut(variables.serverSocket);
		/* If there are messages from both the GUI and the server, then does the
		 * server's message have priority? Toggled with each loop rotation. */
		bool serverPriority = false;
		while (true) {
			serverPriority = !serverPriority;
			variables.messageSemaphore.acquire();

			if ((serverPriority && !variables.serverMessages.empty()) ||
			    variables.inputMessages.empty()) {
				/* Process message from server. */
				variables.serverSemaphore.acquire();
				DataServerMessage inMessage = variables.serverMessages.front();
				variables.serverMessages.pop();
				variables.serverSemaphore.release();

				 GUIBufferOut << processServerMessage(variables, inMessage);
			} else {
				/* Process message from GUI. */
				variables.inputSemaphore.acquire();
				DataInputMessage inMessage = variables.inputMessages.front();
				variables.inputMessages.pop();
				variables.inputSemaphore.release();

				serverBufferOut << processInputMessage(variables, inMessage);
			}
		}
	}
} // namespace

int main(int argc, char ** argv) {
	try {
		/* Install SIGINT handler, prepare options, endpoints, sockets, buffers, and
		 * message queues. Save all to a ClientVariables class. */
		installSignalHandler(SIGINT, throwInterrupt, SA_RESTART);
		ClientVariables variables{argc, argv};

		std::thread GUIListener(listenToGUI, std::ref(variables));
		std::thread serverListener(listenToServer, std::ref(variables));

		std::cout << "Server address: " << variables.serverEndpoint
		          << "\nGUI address: " << variables.GUIEndpoint
		          << "\nListening on port "
		          << variables.options["port"].as<port_t>() << "\n";

		/* Main loop. */
		mainLoop(variables);

	} catch (NeedHelp & e) {
		/* Exception reserved for --help option. */
		std::cout << getClientOptionsDescription();
		return 0;
	} catch (UnrecoverableException & e) {
		/* Something went wrong, we know what it is and cannot recover from it. */
		std::cerr << e.what();
		return 1;
	} catch (std::exception & e) {
		/* Something went wrong and we were not prepared. This is bad. */
		std::cerr << e.what();
		return 2;
	}
}