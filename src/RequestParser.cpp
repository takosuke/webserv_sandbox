#include "RequestParser.hpp"
#include <cctype>
#include <algorithm>


void RequestParser::feed(const char *data, int len) {
	// first let's ignore all error checking and just pretend all requests are
	// perfect
	// we can work on errors later
	// move the next thing into a state machine handler thing
	_buf.append(data, len);
	// checks if state is in request line
	// if so, looks in buf for CRLF
	// if not found, falls through until next feed
	if (_state == REQUEST_LINE)
		parse_request_line();
	// while state is in HEADERS
	// look for CRLF and double CRLF
	// if no single CRLF found, fall through
	// if double CRLF found, move to BODY
	if (_state == HEADERS)
		parse_headers();
	if (_state == BODY)
	{
		parse_content_length();
		parse_body();
	}
	if (_state == COMPLETE || _error)
	{
		LOG_DEBUG("REQUEST") << "Request:" << _req << std::endl;
		_req.printRequest();
		return;
	}
}

void RequestParser::parse_request_line() {
	size_t pos = _buf.find("\r\n");
	if (pos != std::string::npos)
	{
		std::string request_line = _buf.substr(0, pos);
		std::istringstream ss(request_line);
		std::string method, uri, version;
		ss >> method >> uri >> version;
		_req.method = _req.stringToMethod(method);
		_req.uri = uri;
		_req.version = version;
		parse_uri();
		_state = HEADERS;
		_buf.erase(0, pos + 2);
	}
}

void RequestParser::parse_uri() {
	size_t pos = _req.uri.find("?");
	_req.path = _req.uri.substr(0, pos);
	if (pos != std::string::npos) {
		_req.query = _req.uri.substr(pos + 1);
	}

}

void RequestParser::parse_headers() {
	// need to loop as long as pos returns something
	size_t pos = _buf.find("\r\n");
	while (pos != std::string::npos) {
		std::string headers_line = _buf.substr(0, pos);
		std::cout << "headers_line:" << headers_line << std::endl;
		if (headers_line.empty())
		{
			_buf.erase(0, pos + 2);
			_state = BODY;
			return ;
		}
		// make headers into lowercase for consistency and avoiding headaches
		std::transform(headers_line.begin(), headers_line.end(), headers_line.begin(), ::tolower);
		size_t colon = headers_line.find(":");
		std::string key = headers_line.substr(0, colon);
		std::string val = headers_line.substr(colon + 1);
		size_t start = val.find_first_not_of(" \t");
		if (start != std::string::npos)
			val = val.substr(start);
		_req.headers.insert(std::make_pair(key, val));
		_buf.erase(0, pos + 2);
		pos = _buf.find("\r\n");
	}
}

void RequestParser::parse_content_length() {
	std::map<std::string, std::string>::iterator it = _req.headers.find("content-length");
	if (it != _req.headers.end()) {
		std::istringstream iss(it->second);
		iss >> _req.content_length;
	} else {
		_req.content_length = 0;
	}

}
void RequestParser::parse_body() {
	if (_req.content_length == 0)
	{
		_state = COMPLETE;
		_complete = true;
	}
	else if (_buf.size() >= _req.content_length) {
		_req.body = _buf.substr(0, _req.content_length);
		_state = COMPLETE;
		_complete = true;
	}
}
