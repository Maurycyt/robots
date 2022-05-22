#pragma once

#include <map>
#include <set>
#include <vector>

#include "buffer.h"

/*
 * This header file contains the definitions of all classes used
 * store, manipulate, parse and un-parse messages
 * between the Client and either the GUI or the game server.
 */

/*
 * =============================================================================
 *                     General and simple Data classes
 * =============================================================================
 */

/* General class interface for structured data representation. */
template <typename T>
concept Data = requires(Buffer & buffer, T & t) {
	               { buffer << t } -> std::same_as<Buffer &>;
	               { buffer >> t } -> std::same_as<Buffer &>;
               };

template <typename T>
concept ComparableData = Data<T> && requires(T & a, T & b) {
	                                    { a < b } -> std::same_as<bool>;
                                    };

/* Integral leaf nodes for structured data representation. */
class DataU8 {
public:
	uint8_t value{0};

	bool operator<(const DataU8 & other) const {
		return value < other.value;
	}
};

Buffer & operator<<(Buffer & buffer, const DataU8 & data) {
	buffer.writeU8(data.value);
	return buffer;
}

Buffer & operator>>(Buffer & buffer, DataU8 & data) {
	data.value = buffer.readU8();
	return buffer;
}

class DataU16 {
public:
	uint16_t value{0};

	bool operator<(const DataU16 & other) const {
		return value < other.value;
	}
};

Buffer & operator<<(Buffer & buffer, const DataU16 & data) {
	buffer.writeU16(data.value);
	return buffer;
}

Buffer & operator>>(Buffer & buffer, DataU16 & data) {
	data.value = buffer.readU16();
	return buffer;
}

class DataU32 {
public:
	uint32_t value{0};

	bool operator<(const DataU32 & other) const {
		return value < other.value;
	}
};

Buffer & operator<<(Buffer & buffer, const DataU32 & data) {
	buffer.writeU32(data.value);
	return buffer;
}

Buffer & operator>>(Buffer & buffer, DataU32 & data) {
	data.value = buffer.readU32();
	return buffer;
}

/* String leaf node for structured data representation. */
class DataString {
public:
	std::string value;

	bool operator<(const DataString & other) const {
		return value < other.value;
	}
};

Buffer & operator<<(Buffer & buffer, const DataString & data) {
	buffer.writeU8((uint8_t)data.value.size());
	buffer.writeStr((data.value));
	return buffer;
}

Buffer & operator>>(Buffer & buffer, DataString & data) {
	data.value = buffer.readStr(buffer.readU8());
	return buffer;
}

/* Internal list node for structured data representation. */
template <Data T> class DataList {
public:
	std::vector<T> list;
};

template <Data T>
Buffer & operator<<(Buffer & buffer, const DataList<T> & data) {
	buffer.writeU32((uint32_t)data.list.size());
	for (const T & i : data.list) {
		buffer << i;
	}
	return buffer;
}

template <Data T> Buffer & operator>>(Buffer & buffer, DataList<T> & data) {
	size_t size = buffer.readU32();
	for (size_t i = 0; i < size; i++) {
		T t;
		buffer >> t;
		data.list.push_back(t);
	}
	return buffer;
}

/* Internal set node for structured data representation. */
template <ComparableData T> class DataMultiset {
public:
	std::multiset<T> set;
};

template <ComparableData T>
Buffer & operator<<(Buffer & buffer, const DataMultiset<T> & data) {
	buffer.writeU32((uint32_t)data.set.size());
	for (const T & i : data.set) {
		buffer << i;
	}
	return buffer;
}

template <ComparableData T> Buffer & operator>>(Buffer & buffer, DataMultiset<T> & data) {
	size_t size = buffer.readU32();
	for (size_t i = 0; i < size; i++) {
		T t;
		buffer >> t;
		data.set.insert(t);
	}
	return buffer;
}

/* Internal map node for structured data representation. */
template <ComparableData K, Data V> class DataMap {
public:
	std::map<K, V> map;
};

template <ComparableData K, Data V>
Buffer & operator<<(Buffer & buffer, const DataMap<K, V> & data) {
	buffer.writeU32((uint32_t)data.map.size());
	for (const auto & i : data.map) {
		buffer << i.first << i.second;
	}
	return buffer;
}

template <ComparableData K, Data V>
Buffer & operator>>(Buffer & buffer, DataMap<K, V> & data) {
	data.map.clear();
	uint32_t length = buffer.readU32();
	for (unsigned int i = 0; i < length; i++) {
		K key;
		V value;
		buffer >> key >> value;
		data.map.insert({key, value});
	}
	return buffer;
}

/*
 * =============================================================================
 *                          Specific Data classes
 * =============================================================================
 */

class DataPlayer {
public:
	DataString name;
	DataString address;

	bool operator<(const DataPlayer & other) const {
		if (name < other.name) {
			return true;
		}
		if (other.name < name) {
			return false;
		}
		return address < other.address;
	}
};

Buffer & operator<<(Buffer & buffer, const DataPlayer & data) {
	return buffer << data.name << data.address;
}

Buffer & operator>>(Buffer & buffer, DataPlayer & data) {
	return buffer >> data.name >> data.address;
}

enum class DirectionEnum : uint8_t {
	Up = 0,
	Right = 1,
	Down = 2,
	Left = 3
};

class DataDirection {
public:
	DirectionEnum direction{0};
};

Buffer & operator<<(Buffer & buffer, const DataDirection & data) {
	buffer.writeU8(static_cast<uint8_t>(data.direction));
	return buffer;
}

Buffer & operator>>(Buffer & buffer, DataDirection & data) {
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 3) {
		throw BadType();
	}
	data.direction = static_cast<DirectionEnum>(enumValue);
	return buffer;
}

class DataPosition {
public:
	DataU16 x;
	DataU16 y;

	bool operator<(const DataPosition & other) const {
		if (x < other.x) {
			return true;
		}
		if (other.x < x) {
			return false;
		}
		return y < other.y;
	}
};

Buffer & operator<<(Buffer & buffer, const DataPosition & data) {
	return buffer << data.x << data.y;
}

Buffer & operator>>(Buffer & buffer, DataPosition & data) {
	return buffer >> data.x >> data.y;
}

class DataBomb {
public:
	DataPosition position;
	DataU16 timer;

	bool operator<(const DataBomb & other) const {
		if (timer < other.timer) {
			return true;
		}
		if (other.timer < timer) {
			return false;
		}
		return position < other.position;
	}
};

Buffer & operator<<(Buffer & buffer, const DataBomb & data) {
	return buffer << data.position << data.timer;
}

Buffer & operator>>(Buffer & buffer, DataBomb & data) {
	return buffer >> data.position >> data.timer;
}

enum class EventEnum : uint8_t {
	BombPlaced = 0,
	BombExploded = 1,
	PlayerMoved = 2,
	BlockPlaced = 3
};

class DataEvent {
public:
	EventEnum type{0};

	DataU32 bombID;
	DataPosition position;
	DataList<DataU8> playersDestroyed;
	DataList<DataPosition> blocksDestroyed;
	DataU8 playerID;
};

Buffer & operator>>(Buffer & buffer, DataEvent & data) {
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 3) {
		throw BadType();
	}
	data.type = static_cast<EventEnum>(enumValue);
	switch (data.type) {
	case EventEnum::BombPlaced:
		return buffer >> data.bombID >> data.position;
	case EventEnum::BombExploded:
		return buffer >> data.bombID >> data.playersDestroyed >>
		       data.blocksDestroyed;
	case EventEnum::PlayerMoved:
		return buffer >> data.playerID >> data.position;
	case EventEnum::BlockPlaced:
		return buffer >> data.position;
	default:
		return buffer;
	}
}

Buffer & operator<<(Buffer & buffer, const DataEvent & data) {
	buffer.writeU8(static_cast<uint8_t>(data.type));
	switch (data.type) {
	case EventEnum::BombPlaced:
		return buffer << data.bombID << data.position;
	case EventEnum::BombExploded:
		return buffer << data.bombID << data.playersDestroyed
		              << data.blocksDestroyed;
	case EventEnum::PlayerMoved:
		return buffer << data.playerID << data.position;
	case EventEnum::BlockPlaced:
		return buffer << data.position;
	default:
		return buffer;
	}
}

/*
 * =============================================================================
 *                            Sendable Data classes
 * =============================================================================
 */

/*
 * These are the four message types mentioned in the project statement.
 * They automatically load data from the network into the buffer before parsing.
 * They automatically send themselves after successful pasting.
 */

/*
 * =============================================================================
 *                            Client-Server messages
 * =============================================================================
 */

enum class ClientMessageEnum : uint8_t {
	Join = 0,
	PlaceBomb = 1,
	PlaceBlock = 2,
	Move = 3
};

class DataClientMessage {
public:
	ClientMessageEnum type{0};
	DataString name;
	DataDirection direction;
};

Buffer & operator<<(Buffer & buffer, const DataClientMessage & data) {
	std::cerr << "Sending client message of type "
	          << (int)static_cast<uint8_t>(data.type) << "\n";
	buffer.writeU8(static_cast<uint8_t>(data.type));
	switch (data.type) {
	case ClientMessageEnum::Join:
		return buffer << data.name << bSend;
	case ClientMessageEnum::Move:
		return buffer << data.direction << bSend;
	default:
		return buffer << bSend;
	}
}

Buffer & operator>>(Buffer & buffer, DataClientMessage & data) {
	buffer >> bReceive;
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 3) {
		throw BadType();
	}
	data.type = static_cast<ClientMessageEnum>(enumValue);
	switch (data.type) {
	case ClientMessageEnum::Join:
		return buffer >> data.name;
	case ClientMessageEnum::Move:
		return buffer >> data.direction;
	default:
		return buffer;
	}
}

enum class ServerMessageEnum : uint8_t {
	Hello = 0,
	AcceptedPlayer = 1,
	GameStarted = 2,
	Turn = 3,
	GameEnded = 4
};

class DataServerMessage {
public:
	ServerMessageEnum type{0};

	DataString serverName;
	DataU8 playerCount;
	DataU16 sizeX;
	DataU16 sizeY;
	DataU16 gameLength;
	DataU16 explosionRadius;
	DataU16 bombTimer;
	DataU8 playerID;
	DataPlayer player;
	DataU16 turn;
	DataMap<DataU8, DataPlayer> players;
	DataList<DataEvent> events;
	DataMap<DataU8, DataU32> scores;
};

Buffer & operator<<(Buffer & buffer, const DataServerMessage & data) {
	buffer.writeU8(static_cast<uint8_t>(data.type));
	switch (data.type) {
	case ServerMessageEnum::Hello:
		return buffer << data.serverName << data.playerCount << data.sizeX
		              << data.sizeY << data.gameLength << data.explosionRadius
		              << data.bombTimer << bSend;
	case ServerMessageEnum::AcceptedPlayer:
		return buffer << data.playerID << data.player << bSend;
	case ServerMessageEnum::GameStarted:
		return buffer << data.players << bSend;
	case ServerMessageEnum::Turn:
		return buffer << data.turn << data.events << bSend;
	case ServerMessageEnum::GameEnded:
		return buffer << data.scores << bSend;
	default:
		return buffer;
	}
}

Buffer & operator>>(Buffer & buffer, DataServerMessage & data) {
	buffer >> bReceive;
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 4) {
		throw BadType();
	}
	data.type = static_cast<ServerMessageEnum>(enumValue);
	std::cerr << "Receiving server message of type "
	          << (int)static_cast<uint8_t>(data.type) << "\n";
	switch (data.type) {
	case ServerMessageEnum::Hello:
		return buffer >> data.serverName >> data.playerCount >> data.sizeX >>
		       data.sizeY >> data.gameLength >> data.explosionRadius >>
		       data.bombTimer;
	case ServerMessageEnum::AcceptedPlayer:
		return buffer >> data.playerID >> data.player;
	case ServerMessageEnum::GameStarted:
		return buffer >> data.players;
	case ServerMessageEnum::Turn:
		return buffer >> data.turn >> data.events;
	case ServerMessageEnum::GameEnded:
		return buffer >> data.scores;
	default:
		return buffer;
	}
}

/*
 * =============================================================================
 *                            Client-GUI messages
 * =============================================================================
 */

enum class DrawMessageEnum : uint8_t {
	Lobby = 0,
	Game = 1
};

class DataDrawMessage {
public:
	DrawMessageEnum type{0};

	DataString serverName;
	DataU8 playerCount;
	DataU16 sizeX;
	DataU16 sizeY;
	DataU16 gameLength;
	DataU16 explosionRadius;
	DataU16 bombTimer;
	DataU16 turn;
	DataMap<DataU8, DataPlayer> players;
	DataMap<DataU8, DataPosition> playerPositions;
	DataMultiset<DataPosition> blocks;
	DataList<DataBomb> bombs;
	DataList<DataPosition> explosions;
	DataMap<DataU8, DataU32> scores;
};

Buffer & operator<<(Buffer & buffer, const DataDrawMessage & data) {
	std::cerr << "Sending draw message of type "
	          << (int)static_cast<uint8_t>(data.type) << "\n";
	buffer.writeU8(static_cast<uint8_t>(data.type));
	switch (data.type) {
	case DrawMessageEnum::Lobby:
		return buffer << data.serverName << data.playerCount << data.sizeX
		              << data.sizeY << data.gameLength << data.explosionRadius
		              << data.bombTimer << data.players << bSend;
	case DrawMessageEnum::Game:
		return buffer << data.serverName << data.sizeX << data.sizeY
		              << data.gameLength << data.turn << data.players
		              << data.playerPositions << data.blocks << data.bombs
		              << data.explosions << data.scores << bSend;
	default:
		return buffer;
	}
}

Buffer & operator>>(Buffer & buffer, DataDrawMessage & data) {
	buffer >> bReceive;
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 1) {
		throw BadType();
	}
	data.type = static_cast<DrawMessageEnum>(enumValue);
	switch (data.type) {
	case DrawMessageEnum::Lobby:
		return buffer >> data.serverName >> data.playerCount >> data.sizeX >>
		       data.sizeY >> data.gameLength >> data.explosionRadius >>
		       data.bombTimer >> data.players;
	case DrawMessageEnum::Game:
		return buffer >> data.serverName >> data.sizeX >> data.sizeY >>
		       data.gameLength >> data.turn >> data.players >>
		       data.playerPositions >> data.blocks >> data.bombs >>
		       data.explosions >> data.scores;
	default:
		return buffer;
	}
}

enum class InputMessageEnum : uint8_t {
	PlaceBomb = 0,
	PlaceBlock = 1,
	Move = 2
};

class DataInputMessage {
public:
	InputMessageEnum type{0};
	DataDirection direction;
};

Buffer & operator<<(Buffer & buffer, const DataInputMessage & data) {
	buffer.writeU8(static_cast<uint8_t>(data.type));
	switch (data.type) {
	case InputMessageEnum::Move:
		return buffer << data.direction << bSend;
	default:
		return buffer << bSend;
	}
}

Buffer & operator>>(Buffer & buffer, DataInputMessage & data) {
	buffer >> bReceive;
	uint8_t enumValue = buffer.readU8();
	if (enumValue > 2) {
		throw BadType();
	}
	data.type = static_cast<InputMessageEnum>(enumValue);
	std::cerr << "Receiving input message of type "
	          << (int)static_cast<uint8_t>(data.type) << "\n";
	switch (data.type) {
	case InputMessageEnum::Move:
		return buffer >> data.direction;
	default:
		return buffer;
	}
}
