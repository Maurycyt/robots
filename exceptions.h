#pragma once

#include <exception>
#include <utility>

class UnrecoverableException : public std::exception {
private:
	std::string message;

public:
	explicit UnrecoverableException(std::string newMessage) :
	    message(std::move(newMessage)) {
	}

	[[nodiscard]] const char * what() const noexcept override {
		return message.c_str();
	}
};

/* Exception thrown when parsing a variant message type goes wrong. */
class BadType : public UnrecoverableException {
public:
	BadType() :
	    UnrecoverableException("Error: message type resolution failed.\n") {
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadWrite : public UnrecoverableException {
public:
	BadWrite() :
	    UnrecoverableException("Error: not enough buffer space to write to.\n") {
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadRead : public UnrecoverableException {
public:
	BadRead() :
	    UnrecoverableException("Error: not enough buffered list to read from.\n"
	    ) {
	}
};

/* Exception thrown when user requests help message. */
class NeedHelp : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "Info: user requested for help message.\n";
	}
};
