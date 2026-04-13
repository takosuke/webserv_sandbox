#pragma once
#include "Connection.hpp"
#include "HttpParser.hpp"
#include <stdint.h>

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		void handle(EpollLoop &loop, uint32_t events);
	private:
		std::string	_write_buf;
		size_t	_write_offset;
		HttpParser	_parser;
		void enqueue_response(EpollLoop &loop, const std::string &response);
};
