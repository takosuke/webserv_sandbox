#pragma once
#include "Connection.hpp"
#include <stdint.h>

class ServerConnection : public Connection {
	public:
		ServerConnection() : Connection() { };
		~ServerConnection();

		void handle(uint32_t events);
		struct sockaddr_in addr;
};
