#ifndef ROBOTS_OPTIONS_H
#define ROBOTS_OPTIONS_H

#include <boost/program_options.hpp>

const boost::program_options::options_description & getClientOptionsDescription() {
	using namespace boost::program_options;
	static options_description clientOptionsDescription("Client options");
	static bool initialized = false;

	if (!initialized) {
		clientOptionsDescription.add_options()
			("help,h", "Display this help message")
			("player-name,n", value<std::string>()->required(), "The name identifying you in the game")
			("port,p", value<uint16_t>()->required(), "The port on which the client will be listening")
			("display-address,d", value<std::string>()->required(), "The address of the GUI server")
			("server-address,s", value<std::string>()->required(), "The address of the game server");

		initialized = true;
	}

	return clientOptionsDescription;
};

#endif // ROBOTS_OPTIONS_H