#pragma once
#include <iostream>
#include <string>

//DUMMY PARSER

class	HttpParser {
	public:
		HttpParser() : _complete(false), _error(false) {}
		

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
