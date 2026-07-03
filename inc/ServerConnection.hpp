#pragma once
#include "Connection.hpp"
#include <stdint.h>

class ServerConnection : public Connection {
		// TODO constructors, destructors etc
	public:
		ServerConnection() : Connection() { };

		void handle(uint32_t events);
		struct sockaddr_in addr;
};
