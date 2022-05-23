#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <mutex>
#include <semaphore>
#include <thread>
#include <unordered_map>

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

	/* Thread exception handling. */
	std::exception_ptr exceptionPtr = nullptr;
	std::binary_semaphore exceptionSemaphore{0};

	void handleInterrupt([[maybe_unused]] int signal) {
		try {
			throw UnrecoverableException("\nInterrupted.\n");
		} catch (UnrecoverableException & e) {
			exceptionPtr = std::current_exception();
			exceptionSemaphore.release();
		}
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

		/* Protection mostly for reading and writing to `state` member variable. */
		std::mutex variablesMutex;
		GameState state{GameState::Lobby};
		std::string playerName;

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
			playerName = options["player-name"].as<std::string>();
		}
	};

	const DataClientMessage & processInputMessage(
	    const ClientVariables & variables, const DataInputMessage & inMessage
	) {
		static DataClientMessage outMessage;
		if (variables.state == GameState::Lobby) {
			/* If GameStarted message not received, send Join. */
			outMessage.type = ClientMessageEnum::Join;
			outMessage.name.value = variables.playerName;
		} else {
			/* Otherwise, just forward the message, pretty much. */
			switch (inMessage.type) {
			case InputMessageEnum::PlaceBomb:
				outMessage.type = ClientMessageEnum::PlaceBomb;
				break;
			case InputMessageEnum::PlaceBlock:
				outMessage.type = ClientMessageEnum::PlaceBlock;
				break;
			case InputMessageEnum::Move:
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
		std::set<uint8_t> destroyedPlayers;
		std::set<DataPosition> destroyedBlocks;

		switch (inMessage.type) {
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
			break;
		case ServerMessageEnum::GameStarted:
			variables.state = GameState::Game;
			outMessage.type = DrawMessageEnum::Game;
			outMessage.players = inMessage.players;
			outMessage.playerPositions.map.clear();
			outMessage.blocks.set.clear();

			break;
		case ServerMessageEnum::Turn:
			/* First, decrease counters on bombs and forget previous explosions. */
			for (auto & entry : activeBombs) {
				entry.second.timer.value--;
			}
			outMessage.explosions.set.clear();

			/* Then, process the events. */
			outMessage.turn = inMessage.turn;
			for (const DataEvent & event : inMessage.events.list) {
				DataBomb bomb;
				DataPosition bombPosition;
				uint16_t radius, leftX, rightX, lowY, highY;
				switch (event.type) {
				case EventEnum::BombPlaced:
					bomb.position = event.position;
					bomb.timer = inMessage.bombTimer;
					activeBombs[event.bombID.value] = bomb;
					break;
				case EventEnum::BombExploded:
					/* Get explosion bounds. */
					bombPosition = activeBombs[event.bombID.value].position;
					radius = outMessage.explosionRadius.value;
					leftX = uint16_t(std::max(0, bombPosition.x.value - radius));
					rightX = uint16_t(std::min(
					    outMessage.sizeX.value - 1, bombPosition.x.value + radius
					));
					lowY = uint16_t(std::max(0, bombPosition.y.value - radius));
					highY = uint16_t(std::min(
					    outMessage.sizeY.value - 1, bombPosition.y.value + radius
					));
					/* Add explosions. */
					for (int x = bombPosition.x.value; x >= int(leftX); x--) {
						DataPosition explosion = {{uint16_t(x)}, bombPosition.y};
						outMessage.explosions.set.insert(explosion);
						if (outMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int x = bombPosition.x.value; x <= int(rightX); x++) {
						DataPosition explosion = {{uint16_t(x)}, bombPosition.y};
						outMessage.explosions.set.insert(explosion);
						if (outMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int y = bombPosition.y.value; y >= int(lowY); y--) {
						DataPosition explosion = {bombPosition.x, {uint16_t(y)}};
						outMessage.explosions.set.insert(explosion);
						if (outMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int y = bombPosition.y.value; y <= int(highY); y++) {
						DataPosition explosion = {bombPosition.x, {uint16_t(y)}};
						outMessage.explosions.set.insert(explosion);
						if (outMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					/* Remove bomb from active bombs. */
					activeBombs.erase(event.bombID.value);
					/* Save destroyed players. */
					for (const DataU8 & playerID : event.playersDestroyed.list) {
						destroyedPlayers.insert(playerID.value);
					}
					/* Save destroyed blocks. */
					for (const DataPosition & block : event.blocksDestroyed.list) {
						destroyedBlocks.insert(block);
					}
					break;
				case EventEnum::PlayerMoved:
					outMessage.playerPositions.map[event.playerID] = event.position;
					break;
				case EventEnum::BlockPlaced:
					outMessage.blocks.set.insert(event.position);
					break;
				default:
					break;
				}
			}

			/* Finally, copy new values to the list of bombs. */
			outMessage.bombs.list.clear();
			for (auto & entry : activeBombs) {
				outMessage.bombs.list.push_back(entry.second);
			}
			/* Update scores and blocks */
			for (uint8_t playerID : destroyedPlayers) {
				outMessage.scores.map[{playerID}].value++;
			}
			for (const DataPosition & block : destroyedBlocks) {
				outMessage.blocks.set.erase(block);
			}
			break;
		case ServerMessageEnum::GameEnded:
			variables.state = GameState::Lobby;
			activeBombs.clear();
			outMessage.type = DrawMessageEnum::Lobby;
			outMessage.playerPositions.map.clear();
			outMessage.blocks.set.clear();
			outMessage.bombs.list.clear();
			outMessage.scores = inMessage.scores;
			break;
		default:
			break;
		}

		return outMessage;
	}

	void listenToGUI(ClientVariables & variables) {
		try {
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
		} catch (std::exception & e) {
			exceptionPtr = std::current_exception();
			exceptionSemaphore.release();
		}
	}

	void listenToServer(ClientVariables & variables) {
		try {
			TCPBuffer serverBufferIn(variables.serverSocket);
			UDPBuffer GUIBufferOut(variables.GUISocket, variables.GUIEndpoint);
			DataServerMessage inMessage;

			while (true) {
				serverBufferIn >> inMessage;

				std::lock_guard<std::mutex> lockGuard(variables.variablesMutex);
				GUIBufferOut << processServerMessage(variables, inMessage);
			}
		} catch (std::exception & e) {
			exceptionPtr = std::current_exception();
			exceptionSemaphore.release();
		}
	}
} // namespace

int main(int argc, char ** argv) {
	try {
		/* Install SIGINT handler, prepare options, endpoints, sockets, buffers, and
		 * message queues. Save all to a ClientVariables class. */
		installSignalHandler(SIGINT, handleInterrupt, SA_RESTART);
		ClientVariables variables{argc, argv};

		std::cout << "Connected to server at " << variables.serverEndpoint << ".\n";
		std::cout << "Sending to GUI at " << variables.GUIEndpoint << ".\n";
		std::cout << "Listening to GUI at port "
		          << variables.options["port"].as<port_t>() << ".\n";

		/* Main loops. */
		std::thread GUIListener(listenToGUI, std::ref(variables));
		std::thread serverListener(listenToServer, std::ref(variables));

		/* Listen for exceptions. */
		exceptionSemaphore.acquire();
		GUIListener.detach();
		serverListener.detach();
		std::rethrow_exception(exceptionPtr);
		/* Some resources may be leaked in the case of exceptions other than
		 * NeedHelp, but we are terminating the program with an unrecoverable
		 * exception anyway. This is meant to handle the output neatly. */

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