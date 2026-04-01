#pragma once
#include "ServerBlock.hpp"
#include "HttpParser.hpp"
#include <stdint.h>

class EpollLoop;

class Connection {
	public:
		virtual void handle(EpollLoop &loop, uint32_t events) = 0;
		virtual ~Connection() {}
		int	fd;
		ServerBlock *config;
};

// TODO split to separate files
class ClientConnection : public Connection {
	public:
		void handle(EpollLoop &loop, uint32_t events);
	private:
		std::string	_write_buf;
		size_t	_write_offset;
		HttpParser	_parser;
		void enqueue_response(EpollLoop &loop, const std::string &response);
};

class ServerConnection : public Connection {
	public:
		void handle(EpollLoop &loop, uint32_t events);
};
