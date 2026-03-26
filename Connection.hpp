#pragma once
#include "ServerBlock.hpp"
#include "EpollLoop.hpp"

struct Connection {
	int	fd;
	enum Type { SERVER, CLIENT } type;
	ServerBlock *config;
	std::string write_buf;
	size_t write_offset;
	void enqueue_response(EpollLoop &loop, const std::string &response);
};
