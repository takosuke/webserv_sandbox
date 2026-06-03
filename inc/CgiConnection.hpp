#pragma once

#include "ClientConnection.hpp"
#include "Connection.hpp"

class ClientConnection;

class CgiConnection : public Connection {
	public:
		CgiConnection(ClientConnection *callback)
			: Connection(), _pid(-1), _callback(callback) {}
		~CgiConnection() {}

		void handle(uint32_t events);

		pid_t			_pid;
		std::string		_output;
	
	private:
		ClientConnection *_callback;
};
