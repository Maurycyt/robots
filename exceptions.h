#pragma once

#include <exception>
#include <utility>

/* Exception thrown when parsing a variant message type goes wrong. */
class badType : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "message type resolution failed.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class badWrite : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "not enough buffer space to write to.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class badRead : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "not enough buffered list to read from.";
	}
};

/* Exception thrown when user requests help message. */
class needHelp : public std::exception {
	[[nodiscard]] const char * what() const noexcept override {
		return "user requested for help message.";
	}
};

class unrecoverableException : public std::exception {
private:
	std::string message;

public:
	explicit unrecoverableException(std::string newMessage) :
	    message(std::move(newMessage)) {
	}

	[[nodiscard]] const char * what() const noexcept override {
		return message.c_str();
	}
};
