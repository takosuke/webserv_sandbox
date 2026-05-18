#pragma once
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
//#include "Http.hpp"

#include <netinet/in.h>
#include <stdint.h>

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		ClientConnection() {}
		ClientConnection(int fildes, Http * config, struct sockaddr_in addr) 
			: Connection(fildes, config), listening_addr(addr),
			_parser(RequestParser(config, addr)) { };

		void handle(EpollLoop &loop, uint32_t events);

		struct sockaddr_in listening_addr;

	private:
		Response		_response;

		RequestParser	_parser;
		void enqueue_response(EpollLoop &loop);
};
