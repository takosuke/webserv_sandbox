#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include "Request.hpp"

//DUMMY PARSER
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
		RequestParser() : _state(REQUEST_LINE), _complete(false), _error(false), _req(Request()){}

		// feed function should complain if more data than declared was sent
		// (400 bad request)
		// should feed etc return ints for error codes
		void feed(const char *data, int len);
		void parse_request_line();
		void parse_uri();
		void parse_headers();
		void parse_body();
		void parse_content_length();
		bool complete() const { return _complete;}
		bool error() const { return _error; }
		const std::string &raw() const { return _buf;}
	private:
		std::string _buf;
		State _state;
		bool _complete;
		bool _error;
		Request _req;
};
