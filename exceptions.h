#pragma once

#include <exception>
#include <utility>

class RobotsException : public std::exception {
private:
	std::string message;

public:
	explicit RobotsException(std::string newMessage) :
	    message(std::move(newMessage)) {
	}

	[[nodiscard]] const char * what() const noexcept override {
		return message.c_str();
	}
};

/* Exception thrown when parsing a variant message type goes wrong. */
class BadType : public RobotsException {
public:
	BadType() : RobotsException("Error: message type resolution failed.\n") {
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadWrite : public RobotsException {
public:
	BadWrite() :
	    RobotsException("Error: not enough buffer space to write to.\n") {
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadRead : public RobotsException {
public:
	BadRead() :
	    RobotsException("Error: not enough buffered list to read from.\n") {
	}
};

/* Exception thrown when user requests help message. */
class NeedHelp : public RobotsException {
public:
	NeedHelp() : RobotsException("Info: user requested for help message.\n") {
	}
};

/* Exception thrown when program receives SIGINT. */
class InterruptedException : public RobotsException {
public:
	InterruptedException() : RobotsException("\nInterrupted.\n") {
	}
};