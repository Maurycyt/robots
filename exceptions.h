#pragma once

#include <exception>
#include <utility>

/* Exception thrown when parsing a variant message type goes wrong. */
class BadType : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "Error: message type resolution failed.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadWrite : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "Error: not enough buffer space to write to.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class BadRead : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "Error: not enough buffered list to read from.";
	}
};

/* Exception thrown when user requests help message. */
class NeedHelp : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "Error: user requested for help message.";
	}
};

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
