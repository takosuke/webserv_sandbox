#pragma once
#include "Connection.hpp"
#include "RequestParser.hpp"
//#include "Http.hpp"

#include <netinet/in.h>
#include <stdint.h>

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		void handle(EpollLoop &loop, uint32_t events);
		struct sockaddr_in listening_addr;

	private:
		std::string	_write_buf;
		size_t	_write_offset;

		RequestParser	_parser;
		void enqueue_response(EpollLoop &loop, const std::string &response);
};
