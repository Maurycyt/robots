#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "buffer.h"
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

	class ServerMessageQueue {
	public:
		DataServerMessage message;
		std::shared_ptr<ServerMessageQueue> next;
	};

	class ClientConnection;
	class Server;

	void listenToClient(Server &, ClientConnection &);
	void emitToClient(ClientConnection &);

	class ClientConnection {
	public:
		// Threads handling the connection
		std::thread clientListener;
		std::thread clientEmitter;
		bool disconnected = false;
		bool joined = false;

		// Connection structures
		std::shared_ptr<tcp::socket> clientSocket;
		std::shared_ptr<TCPBuffer> inBuffer;
		std::shared_ptr<TCPBuffer> outBuffer;

		// Message receival members
		std::mutex inMessageMutex{};
		DataClientMessage inMessage;
		bool inMessagePending = false;

		// Message broadcast members
		std::mutex forMessagesMutex{};
		std::condition_variable forMessages{};
		// Initialize with dummy message
		std::shared_ptr<ServerMessageQueue> messageQueueHead =
		    std::make_shared<ServerMessageQueue>();
		std::shared_ptr<ServerMessageQueue> messageQueueTail = messageQueueHead;

		explicit ClientConnection(Server & server) :
		    clientListener(listenToClient, std::ref(server), std::ref(*this)) {
		}

		void pushMessage(std::shared_ptr<ServerMessageQueue> message) {
			{
				std::lock_guard<std::mutex> guard(forMessagesMutex);
				while (messageQueueTail->next) {
					messageQueueTail = messageQueueTail->next;
				}
				if (messageQueueTail != message) {
					messageQueueTail->next = std::move(message);
				}
				while (messageQueueTail->next) {
					messageQueueTail = messageQueueTail->next;
				}
			}
			forMessages.notify_all();
		}

		void notify() {
			forMessages.notify_all();
		}

		~ClientConnection() {
			if (clientListener.joinable()) {
				clientListener.join();
			}
			if (clientEmitter.joinable()) {
				clientEmitter.join();
			}
		}
	};

	class PlayerInfo {
	public:
		size_t connectionID;
		DataPosition position;
		DataString name;
		DataString address;
	};

	class Server {
	public:
		io_context context;

		variables_map options;

		// Protection from connecting at an unfortunate time.
		std::mutex serverMutex{};

		// Server options and auxiliary variables.
		std::string serverName;
		static const uint16_t PLAYER_COUNT_MAX = (1 << 8) - 1; // 255
		uint16_t playerCount;
		uint16_t sizeX, sizeY;
		uint16_t gameLength;
		uint16_t explosionRadius;
		uint16_t bombTimer;
		uint64_t turnDuration;
		uint16_t initialBlocks;
		GameState state = GameState::Lobby;

		// Connection-related members
		tcp::endpoint serverEndpoint;
		tcp::acceptor clientAcceptor;

		size_t nextConnectionID = 0;
		std::map<size_t, ClientConnection> clients;

		std::mutex forMessagesMutex;
		std::condition_variable forMessages;
		int pendingMessages = 0;
		bool isShutdown = false;

		// Game simulation members
		Random random;
		std::set<DataPosition> blocks;
		// sorted by explosion turn number, position, ID
		std::priority_queue<
		    std::pair<DataBomb, DataU32>, std::vector<std::pair<DataBomb, DataU32>>,
		    std::greater<>>
		    bombs;
		uint32_t nextBombID = 0;
		std::vector<PlayerInfo> joinedPlayers;
		DataMap<DataU8, DataU32> playerScores;
		std::map<DataPosition, std::set<DataU8>> playersByPosition;
		std::set<DataPosition> blocksDestroyed;
		std::set<DataU8> playersDestroyed;

		/* Message storage
		 * Hello message is copied for each connection.
		 * The Accepted Player messages form a queue, which then transitions into
		 * a queue of Game messages (Game Started, Turn, Game Ended).
		 * When a new client connects, they either connected before the first
		 * AcceptedPlayer message, or after. In the first case, when the first
		 * AcceptedPlayer message is sent, it is pushed to all connections.
		 * In the second case, new messages are appended to the (one and only)
		 * queue, and all connections are notified about a new message.
		 * When the last player joins, a GameStarted message is prepared immediately
		 * to avoid shenanigans with new connections not having a message ready and
		 * having to be notified when they can send something.
		 */
		DataServerMessage helloMessage;
		std::shared_ptr<ServerMessageQueue> acceptedPlayerMessagesHead = nullptr;
		std::shared_ptr<ServerMessageQueue> currentGameMessagesHead = nullptr;
		std::shared_ptr<ServerMessageQueue> messageQueueTail = nullptr;

		Server(int argc, char ** argv) :
		    context(),
		    options(handleOptions(argc, argv, getServerOptionsDescription())),
		    serverName(options["server-name"].as<std::string>()),
		    playerCount(options["players-count"].as<uint16_t>()),
		    sizeX(options["size-x"].as<uint16_t>()),
		    sizeY(options["size-y"].as<uint16_t>()),
		    gameLength(options["game-length"].as<uint16_t>()),
		    explosionRadius(options["explosion-radius"].as<uint16_t>()),
		    bombTimer(options["bomb-timer"].as<uint16_t>()),
		    turnDuration(options["turn-duration"].as<uint64_t>()),
		    initialBlocks(options["initial-blocks"].as<uint16_t>()),
		    serverEndpoint(tcp::v6(), options["port"].as<port_t>()),
		    clientAcceptor(context, serverEndpoint),
		    random(uint64_t(options["seed"].as<uint32_t>())) {
			if (playerCount > PLAYER_COUNT_MAX) {
				throw RobotsException(
				    "Error: the argument ('" + std::to_string(playerCount) +
				    "') for option '--players-count' is invalid.\n" + "Run " +
				    std::string(argv[0]) + " --help for usage.\n"
				);
			}
			std::stringstream ss;
			ss << "Listening for " << serverEndpoint << "\n";
			debug(ss.str());

			// Prepare hello message.
			helloMessage.type = ServerMessageEnum::Hello;
			helloMessage.serverName = {serverName};
			helloMessage.playerCount = {uint8_t(playerCount)};
			helloMessage.sizeX = {sizeX};
			helloMessage.sizeY = {sizeY};
			helloMessage.gameLength = {gameLength};
			helloMessage.explosionRadius = {explosionRadius};
			helloMessage.bombTimer = {bombTimer};

			// Start first listener.
			clients.emplace(std::make_pair(nextConnectionID++, std::ref(*this)));
		}

		void notifyAllConnections() {
			for (auto & i : clients) {
				i.second.notify();
			}
		}

		void
		pushToAllConnections(const std::shared_ptr<ServerMessageQueue> & message) {
			for (auto & client : clients) {
				client.second.pushMessage(message);
			}
		}

		void joinPlayer(
		    const DataClientMessage & inMessage, const size_t connectionID,
		    const std::string & address
		) {
			// Add player data to joinedPlayers vector.
			auto playerID = uint8_t(joinedPlayers.size());
			joinedPlayers.push_back({
			    connectionID,   // connectionID
			    {},             // position (currently undefined)
			    inMessage.name, // name
			    {address}       // address
			});

			// Add AcceptedPlayer message to be sent to all connected clients.
			DataServerMessage acceptedPlayerMessage;
			acceptedPlayerMessage.type = ServerMessageEnum::AcceptedPlayer;
			acceptedPlayerMessage.playerID = {playerID};
			acceptedPlayerMessage.player = {
			    joinedPlayers[playerID].name, joinedPlayers[playerID].address};

			ServerMessageQueue acceptedPlayerNode{acceptedPlayerMessage, nullptr};
			std::shared_ptr<ServerMessageQueue> nodePointer =
			    std::make_shared<ServerMessageQueue>(acceptedPlayerNode);

			std::lock_guard<std::mutex> sGuard(serverMutex);
			if (acceptedPlayerMessagesHead) {
				// If a player was already accepted, add message to queue, notify.
				messageQueueTail->next = nodePointer;
				messageQueueTail = messageQueueTail->next;
				notifyAllConnections();
			} else {
				// Else, start the queue and push it to all connections.
				acceptedPlayerMessagesHead = nodePointer;
				messageQueueTail = nodePointer;
				pushToAllConnections(acceptedPlayerMessagesHead);
			}
		}

		void collectPlayers() {
			auto connectionIterator = clients.begin();
			while (joinedPlayers.size() < playerCount) {
				{
					// Wait until there will be a pending message
					std::unique_lock<std::mutex> sGuard(forMessagesMutex);
					forMessages.wait(sGuard, [&]() {
						return pendingMessages > 0;
					});
				}

				if (isShutdown) {
					throw InterruptedException();
				}

				// Go back to the beginning if necessary
				if (connectionIterator == clients.end()) {
					connectionIterator = clients.begin();
				}
				auto currentIterator = connectionIterator;
				connectionIterator++;

				// Check iterator "validity". Remove client if invalid. Then, check if
				// connection holds the message we are looking for. If so, process it.
				ClientConnection & connection = currentIterator->second;
				if (connection.disconnected) {
					clients.erase(currentIterator);
				} else if (connection.inMessagePending) {
					// Found message, extract it.
					DataClientMessage inMessage;
					{
						std::lock_guard<std::mutex> cGuard(connection.inMessageMutex);
						std::lock_guard<std::mutex> sGuard(forMessagesMutex);
						inMessage = connection.inMessage;
						connection.inMessagePending = false;
						pendingMessages--;
					}

					// Now, if it is a Join message, process it.
					if (inMessage.type == ClientMessageEnum::Join && !connection.joined) {
						connection.joined = true;
						std::stringstream addressSS;
						addressSS << connection.clientSocket->remote_endpoint();
						joinPlayer(inMessage, currentIterator->first, addressSS.str());
					}
				}
			}
		}

		void startGame() {
			// Acquire protection against new connections choosing message queues.
			std::lock_guard<std::mutex> sGuard(serverMutex);
			state = GameState::Game;

			// Prepare GameStarted message
			DataServerMessage gameStartedMessage;
			gameStartedMessage.type = ServerMessageEnum::GameStarted;
			for (size_t i = 0; i < joinedPlayers.size(); i++) {
				const PlayerInfo & joinedPlayer = joinedPlayers[i];
				DataPlayer player;
				player.name = joinedPlayer.name;
				player.address = joinedPlayer.address;
				gameStartedMessage.players.map.insert({{uint8_t(i)}, player});
			}

			// Push the GameStarted message
			ServerMessageQueue gameStartedNode = {gameStartedMessage, {nullptr}};
			std::shared_ptr<ServerMessageQueue> gameStartedNodePtr =
			    std::make_shared<ServerMessageQueue>(gameStartedNode);
			currentGameMessagesHead = gameStartedNodePtr;
			messageQueueTail->next = currentGameMessagesHead;
			messageQueueTail = messageQueueTail->next;

			// Prepare Turn 0
			DataServerMessage turn0;
			turn0.type = ServerMessageEnum::Turn;
			turn0.turn = {0};
			for (size_t i = 0; i < joinedPlayers.size(); i++) {
				joinedPlayers[i].position = {
				    {uint16_t(random.next() % sizeX)},
				    {uint16_t(random.next() % sizeY)}};
				DataEvent event;
				event.type = EventEnum::PlayerMoved;
				event.playerID = {uint8_t(i)};
				event.position = joinedPlayers[i].position;
				turn0.events.list.push_back(event);
			}
			for (uint16_t i = 0; i < initialBlocks; i++) {
				DataEvent event;
				event.type = EventEnum::BlockPlaced;
				event.position = {
				    {uint16_t(random.next() % sizeX)},
				    {uint16_t(random.next() % sizeY)}};
				if (!blocks.contains(event.position)) {
					blocks.insert(event.position);
					turn0.events.list.push_back(event);
				}
			}

			// Push the Turn message
			ServerMessageQueue turnNode = {turn0, {nullptr}};
			std::shared_ptr<ServerMessageQueue> turnNodePtr =
			    std::make_shared<ServerMessageQueue>(turnNode);
			messageQueueTail->next = turnNodePtr;
			messageQueueTail = messageQueueTail->next;

			// Actually notify the clients
			notifyAllConnections();
		}

		// Processes explosion, returns whether the explosion should continue.
		bool processExplosion(const DataPosition & position, DataEvent & event) {
			for (const auto & i : playersByPosition[position]) {
				event.playersDestroyed.list.push_back(i);
				playersDestroyed.insert(i);
			}
			if (blocks.contains(position)) {
				event.blocksDestroyed.list.push_back(position);
				blocksDestroyed.insert(position);
				return false;
			}
			return true;
		}

		void processExplosions(uint16_t turn, DataServerMessage & turnMessage) {
			while (!bombs.empty() && bombs.top().first.timer.value == turn) {
				DataEvent event;
				event.type = EventEnum::BombExploded;
				DataBomb bomb = bombs.top().first;
				DataU32 bombID = bombs.top().second;
				event.bombID = bombID;
				bombs.pop();

				uint16_t leftX =
				    uint16_t(std::max(0, bomb.position.x.value - explosionRadius));
				uint16_t rightX = uint16_t(
				    std::min(sizeX - 1, bomb.position.x.value + explosionRadius)
				);
				uint16_t lowY =
				    uint16_t(std::max(0, bomb.position.y.value - explosionRadius));
				uint16_t highY = uint16_t(
				    std::min(sizeY - 1, bomb.position.y.value + explosionRadius)
				);
				if (processExplosion(bomb.position, event)) {
					for (int x = bomb.position.x.value - 1; x >= int(leftX); x--) {
						if (!processExplosion({{uint16_t(x)}, bomb.position.y}, event)) {
							break;
						}
					}
					for (int x = bomb.position.x.value + 1; x <= int(rightX); x++) {
						if (!processExplosion({{uint16_t(x)}, bomb.position.y}, event)) {
							break;
						}
					}
					for (int y = bomb.position.y.value - 1; y >= int(lowY); y--) {
						if (!processExplosion({bomb.position.x, {uint16_t(y)}}, event)) {
							break;
						}
					}
					for (int y = bomb.position.y.value + 1; y <= int(highY); y++) {
						if (!processExplosion({bomb.position.x, {uint16_t(y)}}, event)) {
							break;
						}
					}
				}

				turnMessage.events.list.push_back(event);
			}

			for (const DataPosition & block : blocksDestroyed) {
				blocks.erase(block);
			}
		}

		void processPlayerMove(uint8_t playerID, DataServerMessage & turnMessage) {
			DataPosition position = joinedPlayers[playerID].position;
			size_t connID = joinedPlayers[playerID].connectionID;
			std::lock_guard<std::mutex> cGuard(clients.at(connID).inMessageMutex);

			DataEvent event;
			DataPosition newPosition;
			int newX = position.x.value, newY = position.y.value;
			if (playersDestroyed.contains({playerID})) {
				newPosition = {
				    {uint16_t(random.next() % sizeX)},
				    {uint16_t(random.next() % sizeY)}};
				playersByPosition[position].erase({playerID});
				playersByPosition[newPosition].insert({playerID});
				joinedPlayers[playerID].position = newPosition;
				playerScores.map[{playerID}].value++;

				event.type = EventEnum::PlayerMoved;
				event.playerID = {playerID};
				event.position = newPosition;
				turnMessage.events.list.push_back(event);
			} else if (clients.at(connID).inMessagePending) {
				DataClientMessage inMessage = clients.at(connID).inMessage;
				switch (inMessage.type) {
				case ClientMessageEnum::PlaceBomb:
					event.type = EventEnum::BombPlaced;
					event.bombID = {nextBombID++};
					event.position = position;
					turnMessage.events.list.push_back(event);

					bombs.push(
					    {{position, {uint16_t(turnMessage.turn.value + bombTimer)}},
					     event.bombID}
					);
					break;
				case ClientMessageEnum::PlaceBlock:
					if (blocks.contains(position)) {
						break;
					}
					blocks.insert(position);
					event.type = EventEnum::BlockPlaced;
					event.position = position;
					turnMessage.events.list.push_back(event);
					break;
				case ClientMessageEnum::Move:
					switch (inMessage.direction.direction) {
					case DirectionEnum::Left:
						newX = position.x.value - 1;
						break;
					case DirectionEnum::Right:
						newX = position.x.value + 1;
						break;
					case DirectionEnum::Down:
						newY = position.y.value - 1;
						break;
					case DirectionEnum::Up:
						newY = position.y.value + 1;
						break;
					default:
						break;
					}

					if (newX < 0 || newY < 0 || newX >= sizeX || newY >= sizeY ||
					    blocks.contains(
					        newPosition = {{uint16_t(newX)}, {uint16_t(newY)}}
					    )) {
						break;
					}
					joinedPlayers[playerID].position = newPosition;
					playersByPosition[position].erase({playerID});
					playersByPosition[newPosition].insert({playerID});
					event.type = EventEnum::PlayerMoved;
					event.playerID = {playerID};
					event.position = newPosition;
					turnMessage.events.list.push_back(event);
					break;
				default:
					break;
				}
			}

			// Mark message as read.
			if (clients.at(connID).inMessagePending) {
				clients.at(connID).inMessagePending = false;
				std::lock_guard<std::mutex> sGuard(forMessagesMutex);
				pendingMessages--;
			}

			// Free some memory
			if (playersByPosition[position].empty()) {
				playersByPosition.erase(position);
			}
		}

		void runGame() {
			for (uint16_t turn = 1; turn <= gameLength; turn++) {
				// Sleep for some time.
				std::chrono::milliseconds timespan(turnDuration);
				std::this_thread::sleep_for(timespan);

				if (isShutdown) {
					throw InterruptedException();
				}

				std::shared_ptr<ServerMessageQueue> turnMessagePtr =
				    std::make_shared<ServerMessageQueue>();
				turnMessagePtr->message.type = ServerMessageEnum::Turn;
				turnMessagePtr->message.turn = {turn};

				blocksDestroyed.clear();
				playersDestroyed.clear();

				processExplosions(turn, turnMessagePtr->message);

				// Process player moves
				for (size_t i = 0; i < joinedPlayers.size(); i++) {
					processPlayerMove(uint8_t(i), turnMessagePtr->message);
				}

				messageQueueTail->next = turnMessagePtr;
				messageQueueTail = messageQueueTail->next;
				notifyAllConnections();
			}

			std::lock_guard<std::mutex> guard(serverMutex);
			state = GameState::Lobby;

			std::shared_ptr<ServerMessageQueue> gameEndedPtr =
			    std::make_shared<ServerMessageQueue>();
			gameEndedPtr->message.type = ServerMessageEnum::GameEnded;
			gameEndedPtr->message.scores = playerScores;

			messageQueueTail->next = gameEndedPtr;
			messageQueueTail = messageQueueTail->next;
			notifyAllConnections();
		}

		void clearGame() {
			std::lock_guard<std::mutex> guard(serverMutex);
			joinedPlayers.clear();
			blocks.clear();
			playerScores.map.clear();
			playersByPosition.clear();
			while (!bombs.empty()) {
				bombs.pop();
			}

			acceptedPlayerMessagesHead = currentGameMessagesHead = messageQueueTail =
			    nullptr;

			// Clear pending messages, including join messages.
			for (auto & i : clients) {
				ClientConnection & client = i.second;
				std::lock_guard<std::mutex> cGuard(client.inMessageMutex);
				client.joined = false;
				if (client.inMessagePending) {
					client.inMessagePending = false;
					std::lock_guard<std::mutex> sGuard(forMessagesMutex);
					pendingMessages--;
				}
			}
		}

		// Shuts down the server, closes all connections.
		void shutdown() {
			// First, shut down the acceptor.
			::shutdown(clientAcceptor.native_handle(), SHUT_RDWR);

			// Next, force failure in all connection threads.
			for (auto & client : clients) {
				if (client.second.clientSocket) {
					try {
						client.second.clientSocket->shutdown(tcp::socket::shutdown_both);
					} catch (std::exception & e) {
						// OK
					}
					client.second.clientSocket->close();
				}
			}

			// Third, set shutdown flag, notify forMessages in case mainLoop is
			// waiting on it. Otherwise, the server should shut down in the matter of
			// a single turn.
			isShutdown = true;
			{
				std::lock_guard<std::mutex> sGuard(forMessagesMutex);
				pendingMessages++;
			}
			forMessages.notify_all();

			// Join all connection threads in ClientConnection destructors.
			clients.clear();
		}
	};

	void listenToClient(Server & server, ClientConnection & connection) {
		try {
			// Wait to accept a connection.
			connection.clientSocket =
			    std::make_shared<tcp::socket>(server.clientAcceptor.accept());
			boost::asio::ip::tcp::no_delay option(true);
			connection.clientSocket->set_option(option);
			// Once accepted, initialize other connection members, and then create
			// another clientConnection object which will wait for another connection.
			connection.inBuffer =
			    std::make_shared<TCPBuffer>(*connection.clientSocket);
			connection.outBuffer =
			    std::make_shared<TCPBuffer>(*connection.clientSocket);
			connection.clientEmitter =
			    std::thread(emitToClient, std::ref(connection));

			{
				std::lock_guard<std::mutex> guard(server.serverMutex);
				server.clients.emplace(
				    std::make_pair(server.nextConnectionID++, std::ref(server))
				);

				/* Send Hello message (prepared in server) and make sure to append turn
				 * message queue (GameStarted, Turn0, ...) if connected during game. */
				// Copy helloMessage.
				ServerMessageQueue first{server.helloMessage, {}};
				connection.messageQueueHead = connection.messageQueueTail =
				    std::make_shared<ServerMessageQueue>();
				connection.pushMessage(std::make_shared<ServerMessageQueue>(first));
				// If in game, push the current game messages, otherwise, list players.
				if (server.state == GameState::Game) {
					connection.pushMessage(server.currentGameMessagesHead);
				} else {
					connection.pushMessage(server.acceptedPlayerMessagesHead);
				}
			}

			// Now start the listening loop.
			while (true) {
				DataClientMessage inMessage;
				*connection.inBuffer >> inMessage;
				// Every time a message is received, let the server know.
				{
					std::lock_guard<std::mutex> cGuard(connection.inMessageMutex);
					connection.inMessage = inMessage;
					if (!connection.inMessagePending) {
						connection.inMessagePending = true;
						{
							std::lock_guard<std::mutex> sGuard(server.forMessagesMutex);
							server.pendingMessages++;
						}
						server.forMessages.notify_all();
					}
				}
			}
		} catch (std::exception & e) {
			// If something goes wrong, start by closing the socket.
			connection.disconnected = true;
			if (connection.clientSocket) {
				try {
					connection.clientSocket->shutdown(tcp::socket::shutdown_both);
				} catch (std::exception & f) {
					// OK
				}
				connection.clientSocket->close();
			}

			// Then make sure the emitter thread notices the error by feeding it a
			// dummy message, which must point tanother dummy message.
			{
				std::lock_guard<std::mutex> cGaurd(connection.forMessagesMutex);
				connection.messageQueueHead = std::make_shared<ServerMessageQueue>();
				connection.messageQueueHead->next =
				    std::make_shared<ServerMessageQueue>();
			}
			connection.forMessages.notify_all();

			// Lastly, update server's pendingMessages variable.
			{
				std::lock_guard<std::mutex> cGuard(connection.inMessageMutex);
				if (connection.inMessagePending) {
					connection.inMessagePending = false;
					std::lock_guard<std::mutex> sGuard(server.forMessagesMutex);
					server.pendingMessages--;
				}
			}
		}
	}

	void emitToClient(ClientConnection & connection) {
		try {
			while (true) {
				// In a loop, wait for a message ready for sending.
				std::unique_lock<std::mutex> lock(connection.forMessagesMutex);
				connection.forMessages.wait(lock, [&]() {
					return bool(connection.messageQueueHead->next);
				});

				// Send the message.
				connection.messageQueueHead = connection.messageQueueHead->next;
				*connection.outBuffer << connection.messageQueueHead->message;
			}
		} catch (std::exception & e) {
			// If something goes wrong, close the socket.
			connection.disconnected = true;
			try {
				connection.clientSocket->shutdown(tcp::socket::shutdown_both);
			} catch (std::exception & f) {
				// OK
			}
			connection.clientSocket->close();
		}
	}

	void mainLoop(Server & server) {
		try {
			while (true) {
				server.collectPlayers();
				server.startGame();
				server.runGame();
				server.clearGame();
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
	installSignalHandler(SIGINT, handleInterrupt, SA_RESTART);
	std::shared_ptr<Server> server;
	try {
		server = std::make_shared<Server>(argc, argv);
	} catch (NeedHelp & e) {
		/* Exception reserved for --help option. */
		std::cout << getServerOptionsDescription();
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

	std::thread mainThread(mainLoop, std::ref(*server));

	/* Listen for exceptions. */
	try {
		std::unique_lock<std::mutex> guard(exceptionMutex);
		exceptionCV.wait(guard, [] {
			return exceptionPtr != nullptr;
		});
		std::rethrow_exception(exceptionPtr);
	} catch (RobotsException & e) {
		// When the server is interrupted, close the acceptor and all sockets, join
		// threads, notify mainLoop etc.
		server->shutdown();
		mainThread.join();
	}
}
