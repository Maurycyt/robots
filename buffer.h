#pragma once

#include <boost/asio.hpp>
#include <concepts>
#include <iostream>
#include <utility>

#include "exceptions.h"

/*
 * Dummy classes to force receival or sending of list to and from the buffer.
 * Similar to std::flush in iostream.
 */

class BufferForceReceive {};

class BufferForceSend {};

BufferForceReceive bReceive;

BufferForceSend bSend;

/*
 * =============================================================================
 *                                  Buffer
 * =============================================================================
 */

/*
 * A buffer wrapper which prepares list for transfer via network.
 * Handles reading and writing, taking care to convert between endianness.
 * Not instantiable, but serves as a base class for UDP and TCP buffers.
 */
class Buffer {
protected:
	size_t size;
	char * buffer;
	size_t left = 0, right = 0;

	explicit Buffer(size_t newSize) : size(newSize) {
		buffer = new char[size];
	}

	void clear() {
		left = 0, right = 0;
	}

	/* Ensures that there are `bytes` bytes ready to read from the buffer. */
	virtual void pull(size_t bytes) = 0;

	/* Ensures that there are `bytes` bytes ready to write into the buffer. */
	virtual void push(size_t bytes) = 0;

	virtual void receive([[maybe_unused]] size_t bytes) = 0;

	virtual void send() = 0;

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
	 * As long as there is some part of the string left to write, make sure that
	 * there is space to write some of its characters to the buffer. Once the
	 * space is made, copy the memory contents, taking proper displacements into
	 * account.
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
		return be16toh(
		    *(uint16_t *)(buffer + (left += sizeof(uint16_t)) - sizeof(uint16_t))
		);
	}

	uint32_t readU32() {
		pull(sizeof(uint32_t));
		return be32toh(
		    *(uint32_t *)(buffer + (left += sizeof(uint32_t)) - sizeof(uint32_t))
		);
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

	Buffer & operator>>([[maybe_unused]] const BufferForceReceive & rec) {
		receive(0);
		return *this;
	}

	Buffer & operator<<([[maybe_unused]] const BufferForceSend & rec) {
		send();
		return *this;
	}

	virtual ~Buffer() {
		delete[] buffer;
	}
};

/* Wrapper for a buffer associated with a UDP connection. */
class UDPBuffer : public Buffer {
private:
	static const int UDP_BUFFER_SIZE = 65507;
	boost::asio::ip::udp::socket & socket;
	boost::asio::ip::udp::endpoint endpoint;

	void receive([[maybe_unused]] size_t bytes) override {
		clear();
		right = socket.receive(boost::asio::buffer(buffer, size));
	}

	void send() override {
		socket.send_to(boost::asio::buffer(buffer, right), endpoint);
		clear();
	}

public:
	UDPBuffer(
	    boost::asio::ip::udp::socket & newSocket,
	    boost::asio::ip::udp::endpoint newEndpoint
	) :
	    Buffer(UDP_BUFFER_SIZE),
	    socket(newSocket), endpoint(std::move(newEndpoint)) {
	}

	void pull(const size_t bytes) override {
		if (right - left < bytes) {
			throw BadRead();
		}
	}

	void push(const size_t bytes) override {
		if (size - right < bytes) {
			throw BadWrite();
		}
	}
};

/* Wrapper for a buffer associated with a TCP connection. */
class TCPBuffer : public Buffer {
private:
	static const int TCP_BUFFER_SIZE = 2048;
	boost::asio::ip::tcp::socket & socket;
	boost::system::error_code error;

	void receive(size_t bytes) override {
		if (bytes == 0) {
			return;
		}
		boost::asio::read(
		    socket, boost::asio::buffer(buffer + right, bytes), error
		);
		if (error == boost::asio::error::eof) {
			throw BadRead(); // Connection closed cleanly by peer, although
			                 // unrightfully.
		} else if (error) {
			throw boost::system::system_error(error); // Other error.
		}
		right += bytes;
	}

	void send() override {
		if (right - left == 0) {
			return;
		}
		boost::asio::write(
		    socket, boost::asio::buffer(buffer + left, right - left)
		);
		clear();
	}

public:
	explicit TCPBuffer(boost::asio::ip::tcp::socket & newSocket) :
	    Buffer(TCP_BUFFER_SIZE), socket(newSocket) {
	}

	/*
	 * Guarantees that there are at least `bytes` bytes to read by
	 * either receiving enough to fulfill that need or by first copying
	 * the received-but-not-read bytes to the beginning and then receiving.
	 */
	void pull(const size_t bytes) override {
//		std::cerr << "Pulling " << bytes << " bytes.\n";
		if (left + bytes > size) {
			memmove(buffer, buffer + left, right - left);
			right -= left;
			left = 0;
		}
		if (right - left < bytes) {
			receive(bytes - (right - left));
		}
	}

	void push(const size_t bytes) override {
//		std::cerr << "Pushing " << bytes << " bytes.\n";
		if (right + bytes > size) {
			send();
		}
	}
};
