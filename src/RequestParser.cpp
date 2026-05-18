#include "RequestParser.hpp"
#include <cctype>
#include <algorithm>
#include "Logger.hpp"
#include "Request.hpp"

static bool isTokenChar(unsigned char c) {
	static const std::string separators = "()<>@,;:\\\"/[]?={} \t";

	if (c >= 127)	return false;
	if (c <= 31)	return false;
	return separators.find(static_cast<char>(c)) == std::string::npos;
}

static bool isValidToken(const std::string& token) {
	if (token.empty()) return false;
	for (size_t i = 0; i < token.size(); ++i)
		if (!isTokenChar(static_cast<unsigned char>(token[i])))
			return false;
	return true;
}

static bool isURITokenChar(unsigned char c) {
	if (c >= 127)	return false;
	if (c <= 32)	return false;
	return true;
}

static bool isValidHex(char a, char b) {
	//TODO 
	return std::isxdigit((unsigned char)a) && std::isxdigit((unsigned char)b);
}

static int validateVersion(const std::string& version) {
	// should we allow extra 0s after?it's officially valid according to RFC
	// 2145
	if (version.size() > 8)
		return 400;
	if (version == "HTTP/1.0" || version == "HTTP/1.1")
		return 0;
	else if (version == "HTTP/2.0")
		return 505;
	else
		return 400;

}

static bool validateReqPath(const std::string& path) {
	// path validation logic
	// check for: malformed %HH encodes, reject literal CTLs and nonascii
	// resolve . and ..
	// its up to router to check if path stays within root
	if (path.empty()) return false;
	for (size_t i = 0; i < path.size(); ++i)
		if (!isURITokenChar(static_cast<unsigned char>(path[i])))
			return false;
	for (size_t i = 0; i < path.size(); ++i) {
		if (path[i] == '%') {
			if (i + 2 >= path.size())
				return false;
			if (!isValidHex(path[i + 1], path[i + 2]))
				return false;
		}
	}
	return true;
}

/**	@brief Adds to `_buf` from `data`.
 */
void RequestParser::feed(const char *data, int len) {
	_buf.append(data, len);
	if (_buf.find("\r\n") == std::string::npos)
		return ;
	if (state() == REQUEST_LINE && parse_request_line() != 0)
		return ;
	if (state() == HEADERS)
		parse_headers();
	if (state() == BODY) {
		if (_req.server == NULL && _http != NULL) {
			_req.server = &(_http->get_server(_addr, getRequest().host));
			_req.location = &(_req.server->get_location(getRequest().path));
		}
		parse_content_length();
		parse_body();
	}
}

int RequestParser::parse_request_line() {
	// TODO
	// check that version is HTTP/1.0 or 1.1, otherwise 400, 505 if it's 2.0
	// enforce a max URI size, check for it
	// method should have more granular error checking?
	// check URI char validity (no raw control, no null, percent encoding
	// something - every % followed by 2 hex digits)
	// path traversal - check paths are /../ --> router responsibility
	size_t pos = _buf.find("\r\n");
	if (pos != std::string::npos)
	{
		// checking that there's 3 tokens, with only one space between each
		std::string request_line = _buf.substr(0, pos);
		size_t sp_first = request_line.find(' ');
		size_t sp_second = request_line.find(' ', sp_first + 1);
		if (sp_first == std::string::npos || sp_second == std::string::npos)
			return (set_error(400));
		if (request_line.find(' ', sp_second + 1) != std::string::npos)
			return (set_error(400));
		std::string method = request_line.substr(0, sp_first);
		if (!isValidToken(method))
			return (set_error(400));
		_req.method = _req.stringToMethod(method);
		if (_req.method == UNKNOWN)
			return (set_error(501));
		_req.uri = request_line.substr(sp_first + 1, sp_second - sp_first - 1);
		_req.version = request_line.substr(sp_second + 1);
		if (validateVersion(_req.version))
			return (set_error(validateVersion(_req.version)));
		if (_req.uri.empty() || _req.version.empty())
			return (set_error(400));

		if (!parse_uri())
			return (set_error(400));
		_state = HEADERS;
		_buf.erase(0, pos + 2);
	}
	return 0;
}

bool RequestParser::parse_uri() {
	size_t pos = _req.uri.find("?");
	// TODO resolve dot segments
	// Root escape check belongs to router?
	_req.path = _req.uri.substr(0, pos);
	if (!(validateReqPath(_req.path)))
		return false;
	// query string sent as is to CGI handler who is supposed to do the percent
	// decoding etc
	if (pos != std::string::npos) {
		_req.query = _req.uri.substr(pos + 2);
	}
	return true;

}

int RequestParser::parse_headers() {
	// need to loop as long as pos returns something
	size_t pos = _buf.find("\r\n");
	while (pos != std::string::npos) {
		std::string headers_line = _buf.substr(0, pos);
		std::cout << "headers_line:" << headers_line << std::endl;
		if (headers_line.empty())
		{
			_buf.erase(0, pos + 2);
			_state = BODY;
			return 0;
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
	return 0;
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

int RequestParser::set_error(int code) {
	_req.error = code;
	return (code);
}
