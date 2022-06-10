#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "buffer.h"
#include "exceptions.h"
#include "messages.h"
#include "options.h"
#include "utils.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::program_options;

namespace {
	/* Thread exception handling. */
	std::exception_ptr exceptionPtr = nullptr;
	std::mutex exceptionMutex;
	std::condition_variable exceptionCV;

	void handleInterrupt([[maybe_unused]] int signal) {
		try {
			throw InterruptedException();
		} catch (InterruptedException & e) {
			{
				std::lock_guard<std::mutex> guard(exceptionMutex);
				exceptionPtr = std::current_exception();
			}
			exceptionCV.notify_one();
		}
	}

	class Client {
	public:
		/* Communication related variables. */
		io_context context;

		variables_map options;

		udp::resolver UDPResolver;
		tcp::resolver TCPResolver;
		udp::endpoint GUIEndpoint;
		tcp::endpoint serverEndpoint;
		udp::socket GUISocket;
		tcp::socket serverSocket;

		/* Protection mostly for reading and writing to `state` member variable. */
		std::mutex variablesMutex;
		GameState state;
		std::string playerName;

		Client(int argc, char ** argv) :
		    context(),
		    options(handleOptions(argc, argv, getClientOptionsDescription())),
		    UDPResolver(context), TCPResolver(context),
		    GUIEndpoint(resolveAddress<udp::endpoint, udp::resolver>(
		        UDPResolver, options["gui-address"].as<std::string>(),
		        std::string(argv[0])
		    )),
		    serverEndpoint(resolveAddress<tcp::endpoint, tcp::resolver>(
		        TCPResolver, options["server-address"].as<std::string>(),
		        std::string(argv[0])
		    )),
		    GUISocket(udp::socket(
		        context, udp::endpoint(udp::v6(), options["port"].as<port_t>())
		    )),
		    serverSocket(context), state(GameState::Lobby),
		    playerName(options["player-name"].as<std::string>()) {
			try {
				serverSocket.connect(serverEndpoint);
				boost::asio::ip::tcp::no_delay option(true);
				serverSocket.set_option(option);
			} catch (std::exception & e) {
				throw RobotsException("Error: " + std::string(e.what()) + "\n");
			}
		}

	private:
		DataClientMessage outClientMessage;

	public:
		const DataClientMessage &
		processInputMessage(const DataInputMessage & inMessage) {
			if (state == GameState::Lobby) {
				/* If GameStarted message not received, send Join. */
				outClientMessage.type = ClientMessageEnum::Join;
				outClientMessage.name.value = playerName;
			} else {
				/* Otherwise, just forward the message, pretty much. */
				switch (inMessage.type) {
				case InputMessageEnum::PlaceBomb:
					outClientMessage.type = ClientMessageEnum::PlaceBomb;
					break;
				case InputMessageEnum::PlaceBlock:
					outClientMessage.type = ClientMessageEnum::PlaceBlock;
					break;
				case InputMessageEnum::Move:
					outClientMessage.type = ClientMessageEnum::Move;
					outClientMessage.direction = inMessage.direction;
					break;
				default:
					break;
				}
			}
			return outClientMessage;
		}

	private:
		DataDrawMessage outDrawMessage;

		std::unordered_map<uint32_t, DataBomb> activeBombs;
		std::set<uint8_t> destroyedPlayers;
		std::set<DataPosition> destroyedBlocks;

		void processTurnMessage(const DataServerMessage & inMessage) {
			/* First, decrease counters on bombs and forget previous explosions. */
			for (auto & entry : activeBombs) {
				entry.second.timer.value--;
			}
			outDrawMessage.explosions.set.clear();

			/* Then, process the events. */
			outDrawMessage.turn = inMessage.turn;
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
					radius = outDrawMessage.explosionRadius.value;
					leftX = uint16_t(std::max(0, bombPosition.x.value - radius));
					rightX = uint16_t(std::min(
					    outDrawMessage.sizeX.value - 1, bombPosition.x.value + radius
					));
					lowY = uint16_t(std::max(0, bombPosition.y.value - radius));
					highY = uint16_t(std::min(
					    outDrawMessage.sizeY.value - 1, bombPosition.y.value + radius
					));
					/* Add explosions. */
					for (int x = bombPosition.x.value; x >= int(leftX); x--) {
						DataPosition explosion = {{uint16_t(x)}, bombPosition.y};
						outDrawMessage.explosions.set.insert(explosion);
						if (outDrawMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int x = bombPosition.x.value; x <= int(rightX); x++) {
						DataPosition explosion = {{uint16_t(x)}, bombPosition.y};
						outDrawMessage.explosions.set.insert(explosion);
						if (outDrawMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int y = bombPosition.y.value; y >= int(lowY); y--) {
						DataPosition explosion = {bombPosition.x, {uint16_t(y)}};
						outDrawMessage.explosions.set.insert(explosion);
						if (outDrawMessage.blocks.set.contains(explosion)) {
							break;
						}
					}
					for (int y = bombPosition.y.value; y <= int(highY); y++) {
						DataPosition explosion = {bombPosition.x, {uint16_t(y)}};
						outDrawMessage.explosions.set.insert(explosion);
						if (outDrawMessage.blocks.set.contains(explosion)) {
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
					outDrawMessage.playerPositions.map[event.playerID] = event.position;
					break;
				case EventEnum::BlockPlaced:
					outDrawMessage.blocks.set.insert(event.position);
					break;
				default:
					break;
				}
			}

			/* Finally, copy new values to the list of bombs. */
			outDrawMessage.bombs.list.clear();
			for (auto & entry : activeBombs) {
				outDrawMessage.bombs.list.push_back(entry.second);
			}
			/* Update scores and blocks */
			for (uint8_t playerID : destroyedPlayers) {
				outDrawMessage.scores.map[{playerID}].value++;
			}
			for (const DataPosition & block : destroyedBlocks) {
				outDrawMessage.blocks.set.erase(block);
			}
		}

	public:
		const DataDrawMessage &
		processServerMessage(const DataServerMessage & inMessage) {
			destroyedPlayers.clear();
			destroyedBlocks.clear();

			switch (inMessage.type) {
			case ServerMessageEnum::Hello:
				outDrawMessage.serverName = inMessage.serverName;
				outDrawMessage.playerCount = inMessage.playerCount;
				outDrawMessage.sizeX = inMessage.sizeX;
				outDrawMessage.sizeY = inMessage.sizeY;
				outDrawMessage.gameLength = inMessage.gameLength;
				outDrawMessage.explosionRadius = inMessage.explosionRadius;
				outDrawMessage.bombTimer = inMessage.bombTimer;
				break;
			case ServerMessageEnum::AcceptedPlayer:
				outDrawMessage.players.map.insert({inMessage.playerID, inMessage.player}
				);
				outDrawMessage.scores.map.insert({inMessage.playerID, {0}});
				break;
			case ServerMessageEnum::GameStarted:
				state = GameState::Game;
				outDrawMessage.type = DrawMessageEnum::Game;
				outDrawMessage.players = inMessage.players;
				outDrawMessage.playerPositions.map.clear();
				outDrawMessage.blocks.set.clear();
				outDrawMessage.scores.map.clear();
				for (auto & player : outDrawMessage.players.map) {
					outDrawMessage.scores.map[player.first] = {0};
				}
				break;
			case ServerMessageEnum::Turn:
				processTurnMessage(inMessage);
				break;
			case ServerMessageEnum::GameEnded:
				state = GameState::Lobby;
				activeBombs.clear();
				outDrawMessage.type = DrawMessageEnum::Lobby;
				outDrawMessage.playerPositions.map.clear();
				outDrawMessage.blocks.set.clear();
				outDrawMessage.bombs.list.clear();
				outDrawMessage.scores = inMessage.scores;
				break;
			default:
				break;
			}

			return outDrawMessage;
		}
	};

	void listenToGUI(Client & variables) {
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
				serverBufferOut << variables.processInputMessage(inMessage);
			}
		} catch (std::exception & e) {
			{
				std::lock_guard<std::mutex> guard(exceptionMutex);
				exceptionPtr = std::current_exception();
			}
			exceptionCV.notify_one();
		}
	}

	void listenToServer(Client & variables) {
		try {
			TCPBuffer serverBufferIn(variables.serverSocket);
			UDPBuffer GUIBufferOut(variables.GUISocket, variables.GUIEndpoint);
			DataServerMessage inMessage;

			while (true) {
				serverBufferIn >> inMessage;

				std::lock_guard<std::mutex> lockGuard(variables.variablesMutex);
				const DataDrawMessage & outMessage =
				    variables.processServerMessage(inMessage);
				if (inMessage.type != ServerMessageEnum::GameStarted) {
					GUIBufferOut << outMessage;
				}
			}
		} catch (std::exception & e) {
			{
				std::lock_guard<std::mutex> guard(exceptionMutex);
				exceptionPtr = std::current_exception();
			}
			exceptionCV.notify_one();
		}
	}
} // namespace

int main(int argc, char ** argv) {
	std::shared_ptr<Client> variables;
	try {
		variables = std::make_shared<Client>(argc, argv);
	} catch (NeedHelp & e) {
		/* Exception reserved for --help option. */
		std::cout << getClientOptionsDescription();
		return 0;
	} catch (RobotsException & e) {
		/* Something went wrong, we know what it is and cannot recover from it, but
		 * do not have to halt the other threads, because there are no threads. */
		std::cerr << e.what();
		return 1;
	} catch (std::exception & e) {
		/* Something went wrong, probably with initializing sockets, like occupied
		 * port. No biggie, just end here. */
		std::cerr << e.what() << "\n";
		return 1;
	}

	/* Install SIGINT handler, prepare options, endpoints, sockets, buffers,
	 * and message queues. Save all to a Client class. */
	installSignalHandler(SIGINT, handleInterrupt, SA_RESTART);

	std::stringstream connectionsInfo;
	connectionsInfo << "Connected to server at " << variables->serverEndpoint
	                << ".\nSending to GUI at " << variables->GUIEndpoint
	                << ".\nListening to GUI on port "
	                << variables->options["port"].as<port_t>() << ".\n";
	debug(connectionsInfo.str());

	/* Main loops. */
	std::shared_ptr<std::thread> GUIListener;
	std::shared_ptr<std::thread> serverListener;

	try {
		GUIListener =
		    std::make_shared<std::thread>(listenToGUI, std::ref(*variables));
		serverListener =
		    std::make_shared<std::thread>(listenToServer, std::ref(*variables));

		/* Listen for exceptions. */
		std::unique_lock<std::mutex> guard(exceptionMutex);
		exceptionCV.wait(guard, [] {
			return exceptionPtr != nullptr;
		});
		std::rethrow_exception(exceptionPtr);
		/* We notify the other threads that the program cannot continue running, and
		 * collect all resources to avoid memory leaks. */

	} catch (RobotsException & e) {
		/* Something went wrong, we know what it is and cannot recover from it.
		 * Close the other threads by forcing exceptions in them. */
		try {
			variables->GUISocket.shutdown(udp::socket::shutdown_both);
		} catch (std::exception & f) {
			// OK
		}
		try {
			variables->serverSocket.shutdown(tcp::socket::shutdown_both);
		} catch (std::exception & f) {
			// OK
		}
		variables->GUISocket.close();
		variables->serverSocket.close();
		GUIListener->join();
		serverListener->join();
		debug(std::string(e.what()));
		return 1;
	}
}