#pragma once

#include "Connection.hpp"
#include "Request.new.hpp"
#include "Response.new.hpp"
#include "ScratchBuffer.hpp"

#include <netinet/in.h>
#include <stdint.h>

#ifndef REDIRECT_LIMIT
# define REDIRECT_LIMIT 5
#endif

class ClientConnection : public Connection {
private:
	ClientConnection() { }; // make private to be uncallable
	
	static std::string	_500_str;
	
	enum {
		REQ_LINE,				// Reading request line
		REQ_HEADERS,			// Reading request headers
		REQ_SETUP,				// Setting up Location, redirections, connections
		REQ_BODY,				// Transmitting request body
		CGI_TRANSMIT_BODY,		// Transmit remaining body to cgi
		CGI_HEADERS,			// Recieve and parse CGI headers
		CGI_BODY,				// Transmit cgi body
		AUTOI_HEADERS,			// Get autoindex headers
		AUTOI_BODY,				// Transmit autoindex body
		RESPONSE				// Transmit response body
	}				_state;

	ScratchBuffer	_buf;
	Request			_req;
	Response		_res;

	struct sockaddr_in	_addr;

	const Server		*_server;
	const Location		*_loc;

	std::string			_file;
	std::fstream		_stream;

	int					_client_fd;
	int					_cgi_stdin_fd;
	int					_cgi_stdout_fd;
	pid_t				_cgi_pid;
	size_t				_written_body;

	bool	handle_req_line();
	bool	handle_req_headers();
	bool	handle_setup();
	bool	handle_response();

	bool	parse_req_headers();
	bool	setup_res();
	bool	setup_cgi();
	bool	setup_autoindex();

	void	handle_cgi_input(uint32_t events);
	bool	handle_cgi_output(uint32_t events);
	void	parse_cgi_headers(size_t sep);
	void	finalize_cgi();

	bool	set_file(const std::string &path);
	size_t	get_file_size() const;

	bool	is_method_allowed() const;
	bool	is_file_existing() const;
	bool	is_dir() const;

	void	fill_res_buffer();
	void	buffer_res_headers();
	void	buffer_file();

	void 	epi_redirect();

	void	setup_internal_error();

public:
	ClientConnection(int sockfd, Http *http_conf, struct sockaddr_in addr);
	~ClientConnection();

	void	handle(uint32_t events);
};
