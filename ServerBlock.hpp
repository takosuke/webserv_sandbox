#pragma once
#include <string>
#include <vector>

struct Location {
	std::string path;
	std::string root;
	std::vector<std::string> allowed_methods;
};

struct ServerBlock {
	ServerBlock();
	ServerBlock(int p, const std::string &h, const std::string &r);
	int	port;
	std::string host;
	std::string root;
	std::vector<Location> locations;
};
