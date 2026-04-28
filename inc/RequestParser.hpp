#pragma once
#include <iostream>
#include <string>

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

class	RequestParser {
	public:
		RequestParser() : _complete(false), _error(false) {}
		

		// feed function should complain if more data than declared was sent
		// (400 bad request)
		void feed(const char *data, int len){
			if (_complete || _error)
				return;
			_buf.append(data, len);
			if (_buf.find("\r\n\r\n") != std::string::npos)
				_complete = true;
		}
		bool complete() const { return _complete;}
		bool error() const { return _error; }
		const std::string &raw() const { return _buf;}
	private:
		std::string _buf;
		bool _complete;
		bool _error;
};
