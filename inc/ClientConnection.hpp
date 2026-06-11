#pragma once

#include "Connection.hpp"
#include "RequestParser.hpp"
#include "Response.hpp"
#include "ScratchBuffer.hpp"

#include <netinet/in.h>
#include <stdint.h>

class ClientConnection : public Connection {
	public:
		// TODO constructors, destructors etc
		ClientConnection(int fildes, Http * config, struct sockaddr_in addr);
		~ClientConnection() { };

		void	handle(uint32_t events);

		void	complete_cgi(const std::string &output);
		void	construct_response(int code);

		struct sockaddr_in listening_addr;

	private:
		ClientConnection() { };

		ScratchBuffer		_buffer;

		RequestParser		_parser;
		Response			_response;

		void	enqueue_response();

		void	handle_get();
		void	handle_cgi();
};
