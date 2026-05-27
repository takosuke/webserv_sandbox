#pragma once

#include "Connection.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"

#include <netinet/in.h>
#include <stdint.h>

class FileConnection;

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		ClientConnection()
			: Connection(), _parser(RequestParser()), _fileconnection(NULL) { }
		ClientConnection(int fildes, Http * config, struct sockaddr_in addr) 
			: Connection(fildes, config), listening_addr(addr),
			_parser(RequestParser(config, addr)), _fileconnection(NULL) { };
		~ClientConnection();

		void handle(uint32_t events);

		void construct_response(int code);

		struct sockaddr_in listening_addr;

	private:
		RequestParser		_parser;

		FileConnection *	_fileconnection;

		Response			_response;
		ResponseStream		_resstream;

		void	enqueue_response();

		void	handle_get();
};

#include "FileConnection.hpp"
