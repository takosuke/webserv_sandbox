#pragma once
#include <iostream>
#include <string>
#include <sstream>

#include "Request.hpp"
#include "ScratchBuffer.hpp"

#define REDIRECT_LIMIT 5

// make an enum of states - request line, headers, body, complete, error
// enum of parse results - complete, incomplete, error
// feed until we have a line
// parse it
// move the state ahead, or to error if something wrong
// when request complete, signal it, flush it, all that
// feed fn does a lot of things
// checks if request header is too big
// checks if it matches the content length
// checks if there
// need fns to parse the request line, the uri, the headers. the body just gets
// passed on?
// parse request line checks that the method is accepted, that the http version
// is accepted, pass uri to parse_uri

enum State {
	REQUEST_LINE,
	HEADERS,
	BODY,
	COMPLETE
};

class	RequestParser {
	public:
		RequestParser()
			: _state(REQUEST_LINE), _complete(false), _req(Request()), _redirects(0),
			_buffer(NULL), _http(NULL), _addr() { }

			/**	@brief Only Construct that should be called for Request Parser. since it also
		 *  sets up the ScratchBuffer to be of the appropriate size. We always use the
		 *  header_buffer size given in the default server for this port, since we can
		 *  only differentiate between the different virtual servers *after* reading the
		 *  headers and checkign for the `Host` header field.
		 */
		RequestParser(Http * config, struct sockaddr_in addr, ScratchBuffer *buf)
			: _state(REQUEST_LINE), _complete(false), _req(Request()), _redirects(0),
			_buffer(buf), _http(config), _addr(addr) {
			_req.server = &(_http->get_default_server(_addr));
			_buffer->set_capacity(_req.server->get_header().buffer_size);
		}

		// feed function should complain if more data than declared was sent
		// (400 bad request)
		// should feed etc return ints for error codes
		void feed(int fd);
		int parse_request_line();
		bool parse_uri();
		int parse_headers();
		void parse_body();
		void parse_content_length();
		void parse_hostname();
		int parse_portstring(std::string portstring);
		bool complete() const { return _complete;}

		void	set_http(const Http * http) { _http = http; };
		void	set_addr(const struct sockaddr_in addr) { _addr = addr; };

		void	set_buffer(ScratchBuffer *buffer);

		int		get_redirects() const { return (_redirects); };
	
		// const std::string &raw() const { return _buffer; };
		const Request& getRequest() const { return _req; };

	private:
		State _state;
		bool _complete;
		Request _req;
		int		_redirects;

		std::fstream	_stream;
		std::string		_file;
		size_t			_written_body_len;

		ScratchBuffer	*_buffer;

		const Http *		_http;
		struct sockaddr_in 	_addr;

		void	clear_buffer();

		bool	error_redirect();
		void	internal_redirect(const std::string &path);

		bool	is_file_existing();
		bool	is_method_allowed();

		/** @brief sets `code` as `error` in _req and returns it.
		 */
		int	set_error(int code);
};
