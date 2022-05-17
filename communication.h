#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <concepts>

/* ============================================================================================= */
/*                                            Buffer                                             */
/* ============================================================================================= */

template<typename T>
concept bufferIntegral =
	std::same_as<T, uint8_t> ||
	std::same_as<T, uint16_t> ||
	std::same_as<T, uint32_t>;

template<typename T>
concept bufferString =
	std::same_as<T, std::string>;

/**
 * @brief A buffer wrapper which prepares data for transfer via network.
 * Handles reading and writing, taking care to convert between endianness.
 */
class Buffer {
private:
	const static size_t BUFFER_SIZE = 1024;
	uint8_t buffer[BUFFER_SIZE] = {};
	size_t length = 0, index = 0;

public:
	void clear() {
		length = 0, index = 0;
	}

	template<bufferIntegral T>
	void write(const T & src) {
		if constexpr (sizeof(T) == 1) {
			*(uint8_t *)(buffer + length) = src;
		} else if constexpr (sizeof(T) == 2) {
			*(uint16_t *)(buffer + length) = htobe16(src);
		} else {
			*(uint32_t *)(buffer + length) = htobe32(src);
		}
		length += sizeof(T);
	}

	template<bufferString T>
	void write(const T & src) {
		strncpy((char *)(buffer + length), src.c_str(), src.size());
		length += src.size();
	}

	template<bufferIntegral T>
	T read() {
		T result;
		if constexpr (sizeof(T) == 1) {
			result = *(uint8_t *)(buffer + index);
		} else if constexpr (sizeof(T) == 2) {
			result = be16toh(*(uint16_t *)(buffer + index));
		} else {
			result = be32toh(*(uint32_t *)(buffer + index));
		}
		length += sizeof(T);
		return result;
	}

	template<bufferString T>
	T read(int len) {
		T result = T((char *)(buffer + index), len);
		index += result.size();
		return result;
	}
};

/* ============================================================================================= */
/*                             General and simple Data classes                                   */
/* ============================================================================================= */

/**
 * @brief General class interface for structured data representation.
 */
class Data {
public:
	virtual void parse(Buffer & buffer) = 0;

	virtual void paste(Buffer & buffer) const = 0;

	virtual ~Data() = default;
};

/**
 * @brief Integral leaf node for structured data representation.
 * @tparam T Integral type of the leaf node.
 */
template<bufferIntegral T>
class DataIntegral : public Data {
public:
	T value;

	void parse(Buffer & buffer) override {
		value = buffer.read<T>();
	}

	void paste(Buffer & buffer) const override {
		buffer.write<T>(value);
	}

	bool operator<(const DataIntegral<T> & other) const {
		return value < other.value;
	}
};

/**
 * @brief String leaf node for structured data representation.
 */
class DataString : public Data {
public:
	std::string data;

	void parse(Buffer & buffer) override {
		uint8_t length = buffer.read<uint8_t>();
		data = buffer.read<std::string>(length);
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint8_t>((uint8_t)data.size());
		buffer.write<std::string>(data);
	}

	bool operator<(const DataString & other) const {
		return data < other.data;
	}
};

/**
 * @brief Internal list node for structured data representation.
 * @tparam T Type of the nodes contained in the list.
 */
template<std::derived_from<Data> T>
class DataList : public Data {
public:
	std::vector<T> data;

	void parse(Buffer & buffer) override {
		data.resize(buffer.read<uint32_t>());
		for (auto & i : data) {
			i.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint32_t>((uint32_t)data.size());
		for (const auto & i : data) {
			i.paste(buffer);
		}
	}
};

/**
 * @brief Internal map node for structured data representation.
 * @tparam K Type of the key nodes contained in the map.
 * @tparam V Type of the value nodes contained int the map.
 */
template<std::derived_from<Data> K, std::derived_from<Data> V>
class DataMap : public Data {
public:
	std::map<K, V> data;

	void parse(Buffer & buffer) override {
		data.clear();
		uint32_t length = buffer.read<uint32_t>();
		for (unsigned int i = 0; i < length; i++) {
			K key;
			V value;
			key.parse(buffer);
			value.parse(buffer);
			data.insert({key, value});
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint32_t>((uint32_t)data.size());
		for (const auto & i : data) {
			i.first.paste(buffer);
			i.second.paste(buffer);
		}
	}
};

/* ============================================================================================= */
/*                            Message-specific Data classes                                      */
/* ============================================================================================= */

class DataPlayer : public Data {
public:
	DataString name;
	DataString address;

	void parse(Buffer & buffer) override {
		name.parse(buffer);
		address.parse(buffer);
	}

	void paste(Buffer & buffer) const override {
		name.paste(buffer);
		address.paste(buffer);
	}

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

enum class DirectionEnum : uint8_t {
	Up = 0,
	Right = 1,
	Down = 2,
	Left = 3
};

class DataDirection : public Data {
public:
	DirectionEnum direction;

	void parse(Buffer & buffer) override {
		direction = static_cast<DirectionEnum>(buffer.read<uint8_t>());
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint8_t>(static_cast<uint8_t>(direction));
	}
};

enum class ClientMessageEnum : uint8_t {
	Join = 0,
	PlaceBomb = 1,
	PlaceBlock = 2,
	Move = 3
};

class DataClientMessage : public Data {
public:
	ClientMessageEnum type;
	DataString name;
	DataDirection direction;

	void parse(Buffer & buffer) override {
		type = static_cast<ClientMessageEnum>(buffer.read<uint8_t>());
		switch(type) {
		case ClientMessageEnum::Join:
			name.parse(buffer);
			break;
		case ClientMessageEnum::Move:
			direction.parse(buffer);
			break;
		default:
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint8_t>(static_cast<uint8_t>(type));
		switch(type) {
		case ClientMessageEnum::Join:
			name.paste(buffer);
			break;
		case ClientMessageEnum::Move:
			direction.paste(buffer);
			break;
		default:
			break;
		}
	}
};

class DataPosition : public Data {
public:
	DataIntegral<uint16_t> x;
	DataIntegral<uint16_t> y;

	void parse(Buffer & buffer) override {
		x.parse(buffer);
		y.parse(buffer);
	}

	void paste(Buffer & buffer) const override {
		x.paste(buffer);
		y.paste(buffer);
	}
};

class DataBomb : public Data {
public:
	DataPosition position;
	DataIntegral<uint16_t> timer;

	void parse(Buffer & buffer) override {
		position.parse(buffer);
		timer.parse(buffer);
	}

	void paste(Buffer & buffer) const override {
		position.paste(buffer);
		timer.paste(buffer);
	}
};

enum class EventEnum : uint8_t {
	BombPlaced = 0,
	BombExploded = 1,
	PlayerMoved = 2,
	BlockPlaced = 3
};

class DataEvent : public Data {
public:
	EventEnum type;
	struct {
		DataIntegral<uint32_t> bombID;
		DataPosition position;
	} bombPlaced;
	struct {
		DataIntegral<uint32_t> bombID;
		DataList<DataIntegral<uint8_t>> playersDestroyed;
		DataList<DataPosition> blocksDestroyed;
	} bombExploded;
	struct {
		DataIntegral<uint8_t> playerID;
		DataPosition position;
	} playerMoved;
	struct {
		DataPosition position;
	} blockPlaced;

	void parse(Buffer & buffer) override {
		type = static_cast<EventEnum>(buffer.read<uint8_t>());
		switch(type) {
		case EventEnum::BombPlaced:
			bombPlaced.bombID.parse(buffer);
			bombPlaced.position.parse(buffer);
			break;
		case EventEnum::BombExploded:
			bombExploded.bombID.parse(buffer);
			bombExploded.playersDestroyed.parse(buffer);
			bombExploded.blocksDestroyed.parse(buffer);
			break;
		case EventEnum::PlayerMoved:
			playerMoved.playerID.parse(buffer);
			playerMoved.position.parse(buffer);
			break;
		case EventEnum::BlockPlaced:
			blockPlaced.position.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint8_t>(static_cast<uint8_t>(type));
		switch (type) {
			case EventEnum::BombPlaced:
				bombPlaced.bombID.paste(buffer);
				bombPlaced.position.paste(buffer);
				break;
			case EventEnum::BombExploded:
				bombExploded.bombID.paste(buffer);
				bombExploded.playersDestroyed.paste(buffer);
				bombExploded.blocksDestroyed.paste(buffer);
				break;
			case EventEnum::PlayerMoved:
				playerMoved.playerID.paste(buffer);
				playerMoved.position.paste(buffer);
				break;
			case EventEnum::BlockPlaced:
				blockPlaced.position.paste(buffer);
		}
	}
};

enum class ServerMessageEnum : uint8_t {
	Hello = 0,
	AcceptedPlayer = 1,
	GameStarted = 2,
	Turn = 3,
	GameEnded = 4
};

class DataServerMessage : public Data {
public:
	ServerMessageEnum type;
	struct {
		DataString serverName;
		DataIntegral<uint8_t> playerCount;
		DataIntegral<uint16_t> sizeX;
		DataIntegral<uint16_t> sizeY;
		DataIntegral<uint16_t> gameLength;
		DataIntegral<uint16_t> explosionRadius;
		DataIntegral<uint16_t> bombTimer;
	} hello;
	struct {
		DataIntegral<uint8_t> playerID;
		DataPlayer player;
	} acceptedPlayer;
	struct {
		DataMap<DataIntegral<uint8_t>, DataPlayer> players;
	} gameStarted;
	struct {
		DataList<DataEvent> events;
	} turn;
	struct {
		DataMap<DataPlayer, DataIntegral<uint32_t>> scores;
	} gameEnded;

	void parse(Buffer & buffer) override {
		type = static_cast<ServerMessageEnum>(buffer.read<uint8_t>());
		switch(type) {
		case ServerMessageEnum::Hello:
			hello.serverName.parse(buffer);
			hello.playerCount.parse(buffer);
			hello.sizeX.parse(buffer);
			hello.sizeY.parse(buffer);
			hello.gameLength.parse(buffer);
			hello.explosionRadius.parse(buffer);
			hello.bombTimer.parse(buffer);
			break;
		case ServerMessageEnum::AcceptedPlayer:
			acceptedPlayer.playerID.parse(buffer);
			acceptedPlayer.player.parse(buffer);
			break;
		case ServerMessageEnum::GameStarted:
			gameStarted.players.parse(buffer);
			break;
		case ServerMessageEnum::Turn:
			turn.events.parse(buffer);
			break;
		case ServerMessageEnum::GameEnded:
			gameEnded.scores.parse(buffer);
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.write<uint8_t>(static_cast<uint8_t>(type));
		switch(type) {
		case ServerMessageEnum::Hello:
			hello.serverName.paste(buffer);
			hello.playerCount.paste(buffer);
			hello.sizeX.paste(buffer);
			hello.sizeY.paste(buffer);
			hello.gameLength.paste(buffer);
			hello.explosionRadius.paste(buffer);
			hello.bombTimer.paste(buffer);
			break;
		case ServerMessageEnum::AcceptedPlayer:
			acceptedPlayer.playerID.paste(buffer);
			acceptedPlayer.player.paste(buffer);
			break;
		case ServerMessageEnum::GameStarted:
			gameStarted.players.paste(buffer);
			break;
		case ServerMessageEnum::Turn:
			turn.events.paste(buffer);
			break;
		case ServerMessageEnum::GameEnded:
			gameEnded.scores.paste(buffer);
			break;
		}
	}
};
