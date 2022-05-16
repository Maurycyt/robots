#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <concepts>

/* ============================================================================================= */
/*                                            BUFFER                                             */
/* ============================================================================================= */

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

	void writeString(const std::string & src) {
		strncpy((char *)(buffer + length), src.c_str(), src.size());
		length += src.size();
	}

	void writeByte(uint8_t src) {
		*(uint8_t *)(buffer + length) = src;
		length += sizeof(src);
	}

	void writeWord(uint16_t src) {
		*(uint16_t *)(buffer + length) = htobe16(src);
		length += sizeof(src);
	}

	void writeDWord(uint32_t src) {
		*(uint32_t *)(buffer + length) = htobe32(src);
		length += sizeof(src);
	}

	std::string readString(size_t len) {
		std::string result = std::string((char *)(buffer + index), len);
		index += result.size();
		return result;
	}

	uint8_t readByte() {
		uint8_t result = *(uint8_t *)(buffer + index);
		index += sizeof(result);
		return result;
	}

	uint16_t readWord() {
		uint16_t result = be16toh(*(uint16_t *)(buffer + index));
		index += sizeof(result);
		return result;
	}

	uint32_t readDWord() {
		uint32_t result = be32toh(*(uint32_t *)(buffer + index));
		index += sizeof(result);
		return result;
	}
};

/* ============================================================================================= */
/*                                    General Data classes                                       */
/* ============================================================================================= */

/**
 * @brief General class for internal structured data representation.
 */
class Data {
public:
	virtual void parse(Buffer & buffer) = 0;

	virtual void paste(Buffer & buffer) const = 0;

	virtual ~Data() = default;
};

class DataU8 : public Data {
public:
	uint8_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readByte();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeByte(value);
	}

	bool operator<(const DataU8 & other) const {
		return value < other.value;
	}
};

class DataU16 : public Data {
public:
	uint16_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readWord();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeWord(value);
	}

	bool operator<(const DataU16 & other) const {
		return value < other.value;
	}
};

class DataU32 : public Data {
public:
	uint32_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readDWord();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeDWord(value);
	}

	bool operator<(const DataU32 & other) const {
		return value < other.value;
	}
};

class DataString : public Data {
public:
	uint8_t length;
	std::string data;

	void parse(Buffer & buffer) override {
		length = buffer.readByte();
		data = buffer.readString(length);
	}

	void paste(Buffer & buffer) const override {
		buffer.writeByte(length);
		buffer.writeString(data);
	}

	bool operator<(const DataString & other) const {
		return data < other.data;
	}
};

template<std::derived_from<Data> T>
class DataList : public Data {
public:
	uint32_t length;
	std::vector<T> data;

	void parse(Buffer & buffer) override {
		length = buffer.readDWord();
		data.resize(length);
		for (T & i : data) {
			i.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeDWord(length);
		for (const T & i : data) {
			i.paste(buffer);
		}
	}
};

template<std::derived_from<Data> K, std::derived_from<Data> V>
class DataMap : public Data {
public:
	uint32_t length;
	std::map<K, V> data;

	void parse(Buffer & buffer) override {
		data.clear();
		length = buffer.readDWord();
		for (unsigned int i = 0; i < length; i++) {
			K key;
			V value;
			key.parse(buffer);
			value.parse(buffer);
			data.insert({key, value});
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeDWord(length);
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
		direction = static_cast<DirectionEnum>(buffer.readByte());
	}

	void paste(Buffer & buffer) const override {
		buffer.writeByte(static_cast<uint8_t>(direction));
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
		type = static_cast<ClientMessageEnum>(buffer.readByte());
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
		buffer.writeByte(static_cast<uint8_t>(type));
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
	uint16_t x, y;

	void parse(Buffer & buffer) override {
		x = buffer.readWord();
		y = buffer.readWord();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeWord(x);
		buffer.writeWord(y);
	}
};

class DataBomb : public Data {
public:
	DataPosition position;
	uint16_t timer;

	void parse(Buffer & buffer) override {
		position.parse(buffer);
		timer = buffer.readWord();
	}

	void paste(Buffer & buffer) const override {
		position.paste(buffer);
		buffer.writeWord(timer);
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
		uint32_t bombID;
		DataPosition position;
	} bombPlaced;
	struct {
		uint32_t bombID;
		DataList<DataU8> playersDestroyed;
		DataList<DataPosition> blocksDestroyed;
	} bombExploded;
	struct {
		uint8_t playerID;
		DataPosition position;
	} playerMoved;
	struct {
		DataPosition position;
	} blockPlaced;

	void parse(Buffer & buffer) override {
		type = static_cast<EventEnum>(buffer.readByte());
		switch(type) {
		case EventEnum::BombPlaced:
			bombPlaced.bombID = buffer.readDWord();
			bombPlaced.position.parse(buffer);
			break;
		case EventEnum::BombExploded:
			bombExploded.bombID = buffer.readDWord();
			bombExploded.playersDestroyed.parse(buffer);
			bombExploded.blocksDestroyed.parse(buffer);
			break;
		case EventEnum::PlayerMoved:
			playerMoved.playerID = buffer.readByte();
			playerMoved.position.parse(buffer);
			break;
		case EventEnum::BlockPlaced:
			blockPlaced.position.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeByte(static_cast<uint8_t>(type));
		switch (type) {
			case EventEnum::BombPlaced:
				buffer.writeDWord(bombPlaced.bombID);
				bombPlaced.position.paste(buffer);
				break;
			case EventEnum::BombExploded:
				buffer.writeDWord(bombExploded.bombID);
				bombExploded.playersDestroyed.paste(buffer);
				bombExploded.blocksDestroyed.paste(buffer);
				break;
			case EventEnum::PlayerMoved:
				buffer.writeByte(playerMoved.playerID);
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
		uint8_t playerCount;
		uint16_t sizeX;
		uint16_t sizeY;
		uint16_t gameLength;
		uint16_t explosionRadius;
		uint16_t bombTimer;
	} hello;
	struct {
		uint8_t playerID;
		DataPlayer player;
	} acceptedPlayer;
	struct {
		DataMap<DataU8, DataPlayer> players;
	} gameStarted;
	struct {
		DataList<DataEvent> events;
	} turn;
	struct {
		DataMap<DataPlayer, DataU32> scores;
	} gameEnded;

	void parse(Buffer & buffer) override {
		type = static_cast<ServerMessageEnum>(buffer.readByte());
		switch(type) {
		case ServerMessageEnum::Hello:
			hello.serverName.parse(buffer);
			hello.playerCount = buffer.readByte();
			hello.sizeX = buffer.readWord();
			hello.sizeY = buffer.readWord();
			hello.gameLength = buffer.readWord();
			hello.explosionRadius = buffer.readWord();
			hello.bombTimer = buffer.readWord();
			break;
		case ServerMessageEnum::AcceptedPlayer:
			acceptedPlayer.playerID = buffer.readByte();
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
		buffer.writeByte(static_cast<uint8_t>(type));
		switch(type) {
		case ServerMessageEnum::Hello:
			hello.serverName.paste(buffer);
			buffer.writeByte(hello.playerCount);
			buffer.writeWord(hello.sizeX);
			buffer.writeWord(hello.sizeY);
			buffer.writeWord(hello.gameLength);
			buffer.writeWord(hello.explosionRadius);
			buffer.writeWord(hello.bombTimer);
			break;
		case ServerMessageEnum::AcceptedPlayer:
			buffer.writeByte(acceptedPlayer.playerID);
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
