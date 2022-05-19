#pragma once

#include "buffer.h"

/*
 * This header file contains the definitions od all classes used
 * store, manipulate, parse and un-parse (paste) messages
 * between the Client and either the GUI or the game server.
 */

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

	DataU32 bombID;
	DataPosition position;
	DataList<DataU8> playersDestroyed;
	DataList<DataPosition> blocksDestroyed;
	DataU8 playerID;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 3) {
			throw badType();
		}
		type = static_cast<EventEnum>(enumValue);
		switch(type) {
		case EventEnum::BombPlaced:
			bombID.parse(buffer);
			position.parse(buffer);
			break;
		case EventEnum::BombExploded:
			bombID.parse(buffer);
			playersDestroyed.parse(buffer);
			blocksDestroyed.parse(buffer);
			break;
		case EventEnum::PlayerMoved:
			playerID.parse(buffer);
			position.parse(buffer);
			break;
		case EventEnum::BlockPlaced:
			position.parse(buffer);
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(type));
		switch (type) {
			case EventEnum::BombPlaced:
				bombID.paste(buffer);
				position.paste(buffer);
				break;
			case EventEnum::BombExploded:
				bombID.paste(buffer);
				playersDestroyed.paste(buffer);
				blocksDestroyed.paste(buffer);
				break;
			case EventEnum::PlayerMoved:
				playerID.paste(buffer);
				position.paste(buffer);
				break;
			case EventEnum::BlockPlaced:
				position.paste(buffer);
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
	
	DataString serverName;
	DataU8 playerCount;
	DataU16 sizeX;
	DataU16 sizeY;
	DataU16 gameLength;
	DataU16 explosionRadius;
	DataU16 bombTimer;
	DataU16 playerID;
	DataPlayer player;
	DataMap<DataU8, DataPlayer> players;
	DataList<DataEvent> events;
	DataMap<DataPlayer, DataU32> scores;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 4) {
			throw badType();
		}
		type = static_cast<ServerMessageEnum>(enumValue);
		switch(type) {
		case ServerMessageEnum::Hello:
			serverName.parse(buffer);
			playerCount.parse(buffer);
			sizeX.parse(buffer);
			sizeY.parse(buffer);
			gameLength.parse(buffer);
			explosionRadius.parse(buffer);
			bombTimer.parse(buffer);
			break;
		case ServerMessageEnum::AcceptedPlayer:
			playerID.parse(buffer);
			player.parse(buffer);
			break;
		case ServerMessageEnum::GameStarted:
			players.parse(buffer);
			break;
		case ServerMessageEnum::Turn:
			events.parse(buffer);
			break;
		case ServerMessageEnum::GameEnded:
			scores.parse(buffer);
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(type));
		switch(type) {
		case ServerMessageEnum::Hello:
			serverName.paste(buffer);
			playerCount.paste(buffer);
			sizeX.paste(buffer);
			sizeY.paste(buffer);
			gameLength.paste(buffer);
			explosionRadius.paste(buffer);
			bombTimer.paste(buffer);
			break;
		case ServerMessageEnum::AcceptedPlayer:
			playerID.paste(buffer);
			player.paste(buffer);
			break;
		case ServerMessageEnum::GameStarted:
			players.paste(buffer);
			break;
		case ServerMessageEnum::Turn:
			events.paste(buffer);
			break;
		case ServerMessageEnum::GameEnded:
			scores.paste(buffer);
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
	DataList<DataPosition> blocks;
	DataList<DataBomb> bombs;
	DataList<DataPosition> explosions;
	DataMap<DataPlayer, DataU32> scores;

	void parse(Buffer & buffer) override {
		uint8_t enumValue = buffer.readU8();
		if (enumValue > 1) {
			throw badType();
		}
		type = static_cast<DrawMessageEnum>(enumValue);
		switch (type) {
		case DrawMessageEnum::Lobby:
			serverName.parse(buffer);
			playerCount.parse(buffer);
			sizeX.parse(buffer);
			sizeY.parse(buffer);
			gameLength.parse(buffer);
			explosionRadius.parse(buffer);
			bombTimer.parse(buffer);
			players.parse(buffer);
			break;
		case DrawMessageEnum::Game:
			serverName.parse(buffer);
			sizeX.parse(buffer);
			sizeY.parse(buffer);
			gameLength.parse(buffer);
			turn.parse(buffer);
			players.parse(buffer);
			playerPositions.parse(buffer);
			blocks.parse(buffer);
			bombs.parse(buffer);
			explosions.parse(buffer);
			scores.parse(buffer);
			break;
		}
	}

	void paste(Buffer & buffer) const override {
		buffer.writeU8(static_cast<uint8_t>(type));
		switch (type) {
		case DrawMessageEnum::Lobby:
			serverName.paste(buffer);
			playerCount.paste(buffer);
			sizeX.paste(buffer);
			sizeY.paste(buffer);
			gameLength.paste(buffer);
			explosionRadius.paste(buffer);
			bombTimer.paste(buffer);
			players.paste(buffer);
			break;
		case DrawMessageEnum::Game:
			serverName.paste(buffer);
			sizeX.paste(buffer);
			sizeY.paste(buffer);
			gameLength.paste(buffer);
			turn.paste(buffer);
			players.paste(buffer);
			playerPositions.paste(buffer);
			blocks.paste(buffer);
			bombs.paste(buffer);
			explosions.paste(buffer);
			scores.paste(buffer);
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
