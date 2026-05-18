#pragma once
#include "Connection.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
//#include "Http.hpp"

#include <netinet/in.h>
#include <stdint.h>

class ClientConnection : public Connection {
	public:
		const Server *		server;
		const Location *	location;

		// TODO constructors, destructors etc
		ClientConnection() : server(NULL), location(NULL) {}

		void handle(EpollLoop &loop, uint32_t events);

		struct sockaddr_in listening_addr;

	private:
		Response		_response;

		RequestParser	_parser;
		void enqueue_response(EpollLoop &loop);
};
