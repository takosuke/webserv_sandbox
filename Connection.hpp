#pragma once
#include "ServerBlock.hpp"

struct Connection {
	int	fd;
	enum Type { SERVER, CLIENT } type;
	ServerBlock *config;
	std::string write_buf;
	size_t write_offset;
	void enqueue_response(int epoll_fd, const std::string &response);
};
