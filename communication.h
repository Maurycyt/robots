#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <concepts>
#include "exceptions.h"

/* ============================================================================================= */
/*                                            Buffer                                             */
/* ============================================================================================= */

/*
 * A buffer wrapper which prepares data for transfer via network.
 * Handles reading and writing, taking care to convert between endianness.
 * Not instantiable, but serves as a base class for UDP and TCP buffers.
 */
class Buffer {
public:
// protected:
	size_t size;
	char * buffer;
	size_t left = 0, right = 0;

	Buffer(const int newSize) : size(newSize) {
		buffer = new char [size];
	}

	void clear() {
		left = 0, right = 0;
	}

	/* Ensures that there are `bytes` bytes ready to read from the buffer. */
	virtual size_t pull(const size_t bytes) = 0;

	/* Ensures that there are `bytes` bytes ready to write into the buffer. */
	virtual size_t push(const size_t bytes) = 0;

	void writeU8(const uint8_t src) {
		push(sizeof(src));
		*(uint8_t *)(buffer + right) = src;
		right += sizeof(uint8_t);
	}

	void writeU16(const uint16_t src) {
		push(sizeof(src));
		*(uint16_t *)(buffer + right) = htobe16(src);
		right += sizeof(uint16_t);
	}

	void writeU32(const uint32_t src) {
		push(sizeof(src));
		*(uint32_t *)(buffer + right) = htobe32(src);
		right += sizeof(uint32_t);
	}

	/*
	 * This one is slightly complicated. While there is some part of the string left to write,
	 * make sure that there is space to write some of its characters to the buffer.
	 * Once the space is made, copy the memory contents, taking proper displacements into account.
	 */
	void writeStr(const std::string & src) {
		const size_t length = src.size();
		size_t written = 0;
		while (written < length) {
			size_t toWrite = std::min(length - written, size);
			push(toWrite);
			memcpy(buffer + right, src.c_str() + written, toWrite);
			written += toWrite;
			right += toWrite;
		}
	}

	uint8_t readU8() {
		pull(sizeof(uint8_t));
		return *(uint8_t *)(buffer + (left += sizeof(uint8_t)) - sizeof(uint8_t));
	}

	uint16_t readU16() {
		pull(sizeof(uint16_t));
		return be16toh(*(uint16_t *)(buffer + (left += sizeof(uint16_t)) - sizeof(uint16_t)));
	}

	uint32_t readU32() {
		pull(sizeof(uint32_t));
		return be32toh(*(uint32_t *)(buffer + (left += sizeof(uint32_t)) - sizeof(uint32_t)));
	}

	std::string readStr(const size_t length) {
		std::string result;
		size_t read = 0;
		while (read < length) {
			size_t toRead = std::min(length - read, size);
			pull(toRead);
			result.append(buffer + left, toRead);
			read += toRead;
			left += toRead;
		}
		return result;
	}

	virtual ~Buffer() {
		delete [] buffer;
	}
};

/* Wrapper for a buffer associated with a UDP connection. */
class UDPBuffer : Buffer {
private:
	static const int UDP_BUFFER_SIZE = 65507;
	boost::asio::ip::udp::endpoint endpoint;

public:
	UDPBuffer(boost::asio::ip::udp::endpoint newEndpoint) :
	Buffer(UDP_BUFFER_SIZE),
	endpoint(newEndpoint) {
	}

	void receive() {

	}

	void send() {

	}
};

/* ============================================================================================= */
/*                             General and simple Data classes                                   */
/* ============================================================================================= */

/* General class interface for structured data representation. */
class Data {
public:
	virtual void parse(Buffer & buffer) = 0;

	virtual void paste(Buffer & buffer) const = 0;

	virtual ~Data() = default;
};

/* Integral leaf nodes for structured data representation. */
class DataU8 : public Data {
public:
	uint8_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readU8();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(value);
	}

	bool operator<(const DataU8 & other) const {
		return value < other.value;
	}
};

class DataU16 : public Data {
public:
	uint16_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readU16();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU16(value);
	}

	bool operator<(const DataU16 & other) const {
		return value < other.value;
	}
};

class DataU32 : public Data {
public:
	uint32_t value;

	void parse(Buffer & buffer) override {
		value = buffer.readU32();
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU32(value);
	}

	bool operator<(const DataU32 & other) const {
		return value < other.value;
	}
};

/* String leaf node for structured data representation. */
class DataString : public Data {
public:
	std::string data;

	void parse(Buffer & buffer) override {
		uint8_t length = buffer.readU8();
		data = buffer.readStr(length);
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8((uint8_t)data.size());
		buffer.writeStr(data);
	}

	bool operator<(const DataString & other) const {
		return data < other.data;
	}
};

/* Internal list node for structured data representation. */
template<std::derived_from<Data> T>
class DataList : public Data {
public:
	std::vector<T> data;

	void parse(Buffer & buffer) override {
		data.resize(buffer.readU32());
		for (auto & i : data) {
			i.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU32((uint32_t)data.size());
		for (const auto & i : data) {
			i.paste(buffer);
		}
	}
};

/* Internal map node for structured data representation. */
template<std::derived_from<Data> K, std::derived_from<Data> V>
class DataMap : public Data {
public:
	std::map<K, V> data;

	void parse(Buffer & buffer) override {
		data.clear();
		uint32_t length = buffer.readU32();
		for (unsigned int i = 0; i < length; i++) {
			K key;
			V value;
			key.parse(buffer);
			value.parse(buffer);
			data.insert({key, value});
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU32((uint32_t)data.size());
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
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 3) {
			throw badType();
		}
		direction = static_cast<DirectionEnum>(enumValue);
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(direction));
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
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 3) {
			throw badType();
		}
		type = static_cast<ClientMessageEnum>(enumValue);
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
		buffer.writeU8(static_cast<uint8_t>(type));
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
	DataU16 x;
	DataU16 y;

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
	DataU16 timer;

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
		DataU32 bombID;
		DataPosition position;
	} bombPlaced;
	struct {
		DataU32 bombID;
		DataList<DataU8> playersDestroyed;
		DataList<DataPosition> blocksDestroyed;
	} bombExploded;
	struct {
		DataU8 playerID;
		DataPosition position;
	} playerMoved;
	struct {
		DataPosition position;
	} blockPlaced;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 3) {
			throw badType();
		}
		type = static_cast<EventEnum>(enumValue);
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
		buffer.writeU8(static_cast<uint8_t>(type));
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
		DataU8 playerCount;
		DataU16 sizeX;
		DataU16 sizeY;
		DataU16 gameLength;
		DataU16 explosionRadius;
		DataU16 bombTimer;
	} hello;
	struct {
		DataU16 playerID;
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
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 4) {
			throw badType();
		}
		type = static_cast<ServerMessageEnum>(enumValue);
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
		buffer.writeU8(static_cast<uint8_t>(type));
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