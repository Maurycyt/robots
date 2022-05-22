#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <unordered_map>
#include <mutex>
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

		std::mutex variablesMutex;

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
			std::cerr << "Trying connection with " << serverEndpoint << "\n";
			serverSocket.connect(serverEndpoint);
			std::cerr << "Connection succeeded!\n";
		}
	};

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
		static std::unordered_map<uint32_t, DataBomb> activeBombs;
		static std::set<uint8_t> destroyedPlayers;

		switch(inMessage.type) {
		case ServerMessageEnum::Hello:
			outMessage.serverName = inMessage.serverName;
			outMessage.playerCount = inMessage.playerCount;
			outMessage.sizeX = inMessage.sizeX;
			outMessage.sizeY = inMessage.sizeY;
			outMessage.gameLength = inMessage.gameLength;
			outMessage.explosionRadius = inMessage.explosionRadius;
			outMessage.bombTimer = inMessage.bombTimer;
			break;
		case ServerMessageEnum::AcceptedPlayer:
			outMessage.players.map.insert({inMessage.playerID, inMessage.player});
			outMessage.scores.map.insert({inMessage.playerID, {0}});
			outMessage.playerCount.value++;
			break;
		case ServerMessageEnum::GameStarted:
			variables.state = GameState::Game;
			outMessage.type = DrawMessageEnum::Game;
			outMessage.players = inMessage.players;
			break;
		case ServerMessageEnum::Turn:
			/* First, decrease counters on bombs and forget destroyed players. */
			for (auto & entry : activeBombs) {
				entry.second.timer.value--;
			}
			destroyedPlayers.clear();

			/* Then, process the events. */
			outMessage.turn = inMessage.turn;
			for (const DataEvent & event : inMessage.events.list) {
				DataBomb bomb;
				switch(event.type) {
				case EventEnum::BombPlaced:
					bomb.position = event.position;
					bomb.timer = inMessage.bombTimer;
					activeBombs[event.bombID.value] = bomb;
					break;
				case EventEnum::BombExploded:
					activeBombs.erase(event.bombID.value);
					for (const DataU8 & playerID : event.playersDestroyed.list) {
						destroyedPlayers.insert(playerID.value);
					}
					for (const DataPosition & block : event.blocksDestroyed.list) {
						outMessage.blocks.set.erase(block);
					}
					break;
				case EventEnum::PlayerMoved:
					outMessage.playerPositions.map[event.playerID] = event.position;
					break;
				case EventEnum::BlockPlaced:
					if (!outMessage.blocks.set.contains(event.position)) {
						outMessage.blocks.set.insert(event.position); //TODO: add set?
					}
					break;
				default:
					break;
				}
			}

			/* Finally, copy new values to the list of bombs and update scores. */
			outMessage.bombs.list.clear();
			for (auto & entry : activeBombs) {
				outMessage.bombs.list.push_back(entry.second);
			}
			for (auto & entry : outMessage.scores.map) {
				if (!destroyedPlayers.contains(entry.first.value)) {
					entry.second.value++;
				}
			}
			break;
		case ServerMessageEnum::GameEnded:
			std::cerr << "Game ended received.\n";
			variables.state = GameState::Lobby;
			activeBombs.clear();
			outMessage.type = DrawMessageEnum::Lobby;
			outMessage.playerPositions.map.clear();
			outMessage.blocks.set.clear();
			outMessage.bombs.list.clear();
//			outMessage.scores = inMessage.scores;
			break;
		default:
			break;
		}

		return outMessage;
	}

	[[noreturn]] void listenToGUI(ClientVariables & variables) {
		UDPBuffer GUIBufferIn(variables.GUISocket, variables.GUIEndpoint);
		TCPBuffer serverBufferOut(variables.serverSocket);
		DataInputMessage inMessage;

		while (true) {
			try {
				GUIBufferIn >> inMessage;
			} catch (BadRead & e) {
				continue;
			} catch (BadType & e) {
				continue;
			}

			std::lock_guard<std::mutex> lockGuard(variables.variablesMutex);

			serverBufferOut << processInputMessage(variables, inMessage);
		}
	}

	[[noreturn]] void listenToServer(ClientVariables & variables) {
		TCPBuffer serverBufferIn(variables.serverSocket);
		UDPBuffer GUIBufferOut(variables.GUISocket, variables.GUIEndpoint);
		DataServerMessage inMessage;

		while (true) {
			serverBufferIn >> inMessage;

			std::lock_guard<std::mutex> lockGuard(variables.variablesMutex);

			GUIBufferOut << processServerMessage(variables, inMessage);
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
		GUIListener.join();
		serverListener.join();

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
		std::cerr << e.what() << "\n";
		return 2;
	}
}