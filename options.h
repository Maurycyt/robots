#pragma once

#include "utils.h"
#include <boost/program_options.hpp>

const boost::program_options::options_description &
getClientOptionsDescription() {
	using namespace boost::program_options;
	static options_description clientOptionsDescription("Client options");
	static bool initialized = false;

	if (!initialized) {
		clientOptionsDescription.add_options()(
		    "help,h", "Display this help message"
		)("gui-address,d", value<std::string>()->required(),
		  "The address of the GUI server"
		)("player-name,n", value<std::string>()->required(),
		  "The name identifying you in the game"
		)("port,p", value<port_t>()->required(),
		  "The port on which the client will be listening"
		)("server-address,s", value<std::string>()->required(),
		  "The address of the game server");

		initialized = true;
	}

	return clientOptionsDescription;
};

const boost::program_options::options_description &
getServerOptionsDescription() {
	using namespace boost::program_options;
	static options_description serverOptionsDescription("Server options");
	static bool initialized = false;

	if (!initialized) {
		serverOptionsDescription.add_options()(
		    "help,h", "Display this help message"
		)("bomb-timer,b", value<uint16_t>()->required(),
		  "The number of turns after which a bomb explodes"
		)("players-count,c", value<uint8_t>()->required(), "The number of players"
		)("turn-duration,t", value<uint64_t>()->required(),
		  "The duration of one turn in milliseconds"
		)("explosion-radius,e", value<uint16_t>()->required(),
		  "The radius of explosions"
		)("initial-blocks,k", value<uint16_t>()->required(),
		  "The initial number of blocks on the board"
		)("game-length,l", value<uint16_t>()->required(),
		  "The length of the game in turns"
		)("server-name,n", value<std::string>()->required(),
		  "The name of the server"
		)("port,p", value<port_t>()->required(),
		  "The port on which the server will be listening"
		)("seed,s", value<uint32_t>()->default_value(0),
		  "The seed to be used during randomization (default is 0)"
		)("size-x,x", value<uint16_t>()->required(),
		  "The horizontal size of the board"
		)("size-y,y", value<uint16_t>()->required(),
		  "The vertical size of the board");

		initialized = true;
	}

	return serverOptionsDescription;
};

boost::program_options::variables_map parseOptions(
    int argc, char ** argv,
    const boost::program_options::options_description & optionsDescription
) {
	using namespace boost::program_options;

	variables_map options;

	/* Parse the arguments, check argument validity. */
	store(
	    command_line_parser(argc, argv).options(optionsDescription).run(), options
	);

	return options;
}

void notifyOptions(boost::program_options::variables_map & options) {
	/* Finalize argument parsing, prepare argument values. */
	boost::program_options::notify(options);
}