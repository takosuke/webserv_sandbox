#pragma once
#include "ServerBlock.hpp"
#include "HttpParser.hpp"
#include <stdint.h>

class EpollLoop;

class Connection {
	public:
		int	fd;
		enum Type { SERVER, CLIENT } type;
		ServerBlock *config;
		std::string write_buf;
		size_t write_offset;
		void enqueue_response(EpollLoop &loop, const std::string &response);
};

class ClientConnection : public Connection {
	public:
		void handle(EpollLoop &loop, uint32_t events);
	private:
		std::string	_write_buf;
		size_t	_write_offset;
		HttpParser	_parser;
};

class ServerConnection : public Connection {
	public:
		void handle(EpollLoop &loop, uint32_t events);
};
