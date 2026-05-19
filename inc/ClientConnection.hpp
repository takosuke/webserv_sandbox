#pragma once

#include "Connection.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
//#include "Http.hpp"

#include <netinet/in.h>
#include <stdint.h>

class FileConnection;

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		ClientConnection()
			: Connection(), _file_fd(-1), _file_connection(NULL),
			_parser(RequestParser()) { }
		ClientConnection(int fildes, Http * config, struct sockaddr_in addr) 
			: Connection(fildes, config), listening_addr(addr),
			_file_fd(-1), _file_connection(NULL),
			_parser(RequestParser(config, addr)) { };
		~ClientConnection();

		void handle(uint32_t events);

		void construct_response(int code);

		struct sockaddr_in listening_addr;

	private:
		Response			_response;
		ResponseStream		_resstream;
		int					_file_fd;
		FileConnection *	_file_connection;

		RequestParser	_parser;

		void enqueue_response();

		void	handle_get();
};

#include "FileConnection.hpp"
