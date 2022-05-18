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
protected:
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
	virtual void pull(const size_t bytes) = 0;

	/* Ensures that there are `bytes` bytes ready to write into the buffer. */
	virtual void push(const size_t bytes) = 0;

public:
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
	 * While there is some part of the string left to write, make sure that there
	 * is space to write some of its characters to the buffer. Once the space is
	 * made, copy the memory contents, taking proper displacements into account.
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

	/* Similar strategy to writeStr. */
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
class UDPBuffer : public Buffer {
private:
	static const int UDP_BUFFER_SIZE = 65507;
	boost::asio::ip::udp::socket & socket;
	boost::asio::ip::udp::endpoint endpoint;

public:
	UDPBuffer(boost::asio::ip::udp::socket & newSocket) :
	Buffer(UDP_BUFFER_SIZE),
	socket(newSocket) {
	}

	void receive() {
		clear();
		right = socket.receive_from(boost::asio::buffer(buffer, size), endpoint);
		std::cerr << "Received from " << endpoint << "\n";
	}

	void send() {
		socket.send_to(boost::asio::buffer(buffer, right), endpoint);
	}

	void pull(const size_t bytes) override {
		if (right - left < bytes) {
			throw badRead();
		}
	}

	void push(const size_t bytes) override {
		if (size - right < bytes) {
			throw badWrite();
		}
	}
};

/* Wrapper for a buffer associated with a TCP connection. */
class TCPBuffer : public Buffer {
private:
	static const int TCP_BUFFER_SIZE = 2048;
	boost::asio::ip::tcp::socket & socket;
	boost::system::error_code error;

public:
	TCPBuffer(boost::asio::ip::tcp::socket & newSocket) :
	Buffer(TCP_BUFFER_SIZE),
	socket(newSocket) {
	}

	void receive(size_t bytes) {
		std::cerr << "Receiving " << bytes << " bytes...\n";
		boost::asio::read(
			socket,
			boost::asio::buffer(buffer + right, bytes),
			error
		);
		if (error == boost::asio::error::eof) {
			throw badRead();  // Connection closed cleanly by peer, although unrightfully.
		} else if (error) {
			throw boost::system::system_error(error); // Other error.
		}
		right += bytes;
	}

	void send() {
		std::cerr << "Sending " << (right - left) << " bytes...\n";
		boost::asio::write(
			socket,
			boost::asio::buffer(buffer + left, right - left)
		);
		clear();
	}

	/*
	 * Guarantess that there are at least `bytes` bytes to read by
	 * either receiving enough to fulfill that need or by first copying
	 * the received-but-not-read bytes to the beginning and then receiving.
	 */
	void pull(const size_t bytes) override {
		if (left + bytes > size) {
			memmove(buffer, buffer + left, right - left);
			right -= left;
			left = 0;
			std::cerr << "Shifting to allow reading " << bytes << " bytes.\n";
		}
		if (right - left < bytes) {
			receive(bytes - (right - left));
		}
	}

	void push(const size_t bytes) override {
		if (right + bytes > size) {
			std::cerr << "Pushing to accomodate " << bytes << " bytes.\n";
			send();
		}
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
/*                                    Specific Data classes                                      */
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

/* ============================================================================================= */
/*                                    Sendable Data classes                                      */
/* ============================================================================================= */
/* 
 * These are the four message types mentioned in the project statement.
 * They automatically send themselves after successful pasting.
 */

/* ============================================================================================= */
/*                                    Client-Server messages                                     */
/* ============================================================================================= */

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

/* ============================================================================================= */
/*                                    Client-GUI messages                                        */
/* ============================================================================================= */

enum class DrawMessageEnum : uint8_t {
	Lobby = 0,
	Game = 1
};

class DataDrawMessage : public Data {
public:
	DrawMessageEnum type;
	struct {
		DataString serverName;
		DataU8 playerCount;
		DataU16 sizeX;
		DataU16 sizeY;
		DataU16 gameLength;
		DataU16 explosionRadius;
		DataU16 bombTimer;
		DataMap<DataU8, DataPlayer> players;
	} lobby;
	struct {
		DataString serverName;
		DataU16 sizeX;
		DataU16 sizeY;
		DataU16 gameLength;
		DataU16 turn;
		DataMap<DataU8, DataPlayer> players;
		DataMap<DataU8, DataPosition> playerPositions;
		DataList<DataPosition> blocks;
		DataList<DataBomb> bombs;
		DataList<DataPosition> explosions;
		DataMap<DataPlayer, DataU32> scores;
	} game;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 1) {
			throw badType();
		}
		type = static_cast<DrawMessageEnum>(enumValue);
		switch (type) {
		case DrawMessageEnum::Lobby:
			lobby.serverName.parse(buffer);
			lobby.playerCount.parse(buffer);
			lobby.sizeX.parse(buffer);
			lobby.sizeY.parse(buffer);
			lobby.gameLength.parse(buffer);
			lobby.explosionRadius.parse(buffer);
			lobby.bombTimer.parse(buffer);
			lobby.players.parse(buffer);
			break;
		case DrawMessageEnum::Game:
			game.serverName.parse(buffer);
			game.sizeX.parse(buffer);
			game.sizeY.parse(buffer);
			game.gameLength.parse(buffer);
			game.turn.parse(buffer);
			game.players.parse(buffer);
			game.playerPositions.parse(buffer);
			game.blocks.parse(buffer);
			game.bombs.parse(buffer);
			game.explosions.parse(buffer);
			game.scores.parse(buffer);
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(type));
		switch (type) {
		case DrawMessageEnum::Lobby:
			lobby.serverName.paste(buffer);
			lobby.playerCount.paste(buffer);
			lobby.sizeX.paste(buffer);
			lobby.sizeY.paste(buffer);
			lobby.gameLength.paste(buffer);
			lobby.explosionRadius.paste(buffer);
			lobby.bombTimer.paste(buffer);
			lobby.players.paste(buffer);
			break;
		case DrawMessageEnum::Game:
			game.serverName.paste(buffer);
			game.sizeX.paste(buffer);
			game.sizeY.paste(buffer);
			game.gameLength.paste(buffer);
			game.turn.paste(buffer);
			game.players.paste(buffer);
			game.playerPositions.paste(buffer);
			game.blocks.paste(buffer);
			game.bombs.paste(buffer);
			game.explosions.paste(buffer);
			game.scores.paste(buffer);
			break;
		}
	}
};

enum class InputMessageEnum : uint8_t {
	PlaceBomb = 0,
	PlaceBlock = 1,
	Move = 2
};

class DataInputMessage : public Data {
public:
	InputMessageEnum type;
	DataDirection direction;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 2) {
			throw badType();
		}
		type = static_cast<InputMessageEnum>(enumValue);
		switch (type) {
		case InputMessageEnum::Move:
			direction.parse(buffer);
			break;
		default:
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(type));
		switch (type) {
		case InputMessageEnum::Move:
			direction.paste(buffer);
			break;
		default:
			break;
		}
	}
};
