#include <iostream>
#include <boost/program_options.hpp>
#include "options.h"

namespace po = boost::program_options;

int main(int argc, char * * argv) {
	/* Parse the program options. */
	const po::options_description & optionsDescription = getClientOptionsDescription();
	po::variables_map options;
	po::store(po::command_line_parser(argc, argv).options(optionsDescription).run(), options);
	po::notify(options);

	std::cerr << options.count("help") << "\n";
	std::cerr << options["display-name"].as<std::string>();
}