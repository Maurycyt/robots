#pragma once

#include <exception>

/* Exception thrown when parsing a variant message type goes wrong. */
class badType : public std::exception {
	const char * what() const noexcept override {
		return "message type resolution failed.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class badWrite : public std::exception {
	const char * what() const noexcept override {
		return "not enough buffer space to write to.";
	}
};

/* Exception thrown when writing exceeds buffer capacity. */
class badRead : public std::exception {
	const char * what() const noexcept override {
		return "not enough buffered data to read from.";
	}
};

/* Exception thrown when user requests help message. */
class needHelp : public std::exception {
	const char * what() const noexcept override {
		return "user requested for help message.";
	}
};
