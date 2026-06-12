#pragma once

#include "Connection.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "ScratchBuffer.hpp"

#include <netinet/in.h>
#include <stdint.h>

class ClientConnection : public Connection {
private:
	ClientConnection() { }; // make private to be uncallable
	
	enum {
		REQ_LINE,				// Reading request line
		REQ_HEADERS,			// Reading request headers
		REQ_SETUP,				// Setting up Location, redirections, connections
		REQ_BODY,				// Transmitting request body
		CGI_TRANSMIT_BODY,		// Transmit remaining body to cgi
		CGI_HEADERS,			// Recieve and parse CGI headers
		CGI_BODY,				// Transmit cgi body
		RES_HEADERS,			// Transmit response headers
		RES_BODY				// Transmit response body
	}				_state;

	ScratchBuffer	_buf;
	Request			_req;
	Response		_res;

	struct sockaddr_in	_addr;

	Server			*_server;
	Location		*_loc;

public:
	ClientConnection(int sockfd, Http *http_conf, struct sockaddr_int addr);
	~ClientConnection();

	void	handle(uint32_t events);
}
