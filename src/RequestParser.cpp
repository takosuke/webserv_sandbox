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
	if (path.empty())
		return false;
	return true;
}

void RequestParser::feed(const char *data, int len) {
	_buf.append(data, len);
	if (_state == REQUEST_LINE)
		_req.error = parse_request_line();
	if (_state == HEADERS)
		_req.error = parse_headers();
	if (_state == BODY)
	{
		parse_content_length();
		parse_body();
	}
	if (_state == COMPLETE || _req.error)
	{
		LOG_DEBUG("REQUEST") << "Request:" << _req << std::endl;
		_req.printRequest();
		return;
	}
}

int RequestParser::parse_request_line() {
	// TODO
	// check that version is HTTP/1.0 or 1.1, otherwise 400, 505 if it's 2.0
	// enforce a max URI size, check for it
	// method should have more granular error checking?
	// check URI char validity (no raw control, no null, percent encoding
	// something - every % followed by 2 hex digits)
	// path traversal - check paths are /../
	size_t pos = _buf.find("\n");
	// we check if the full CLRF is there
	if (_buf[pos - 1] != '\r')
		return 400;
	if (pos != std::string::npos)
	{
		// checking that there's 3 tokens, with only one space between each
		std::string request_line = _buf.substr(0, pos - 1);
		size_t sp_first = request_line.find(' ');
		size_t sp_second = request_line.find(' ', sp_first + 1);
		if (sp_first == std::string::npos || sp_second == std::string::npos)
			return 400;
		if (request_line.find(' ', sp_second + 1) != std::string::npos)
			return 400;
		std::string method = request_line.substr(0, sp_first);
		if (!isValidToken(method))
			return 400;
		_req.method = _req.stringToMethod(method);
		if (_req.method == UNKNOWN)
			return 501;
		_req.uri = request_line.substr(sp_first + 1, sp_second - sp_first - 1);
		_req.version = request_line.substr(sp_second + 1);
		if (validateVersion(_req.version))
			return (validateVersion(_req.version));
		if (_req.uri.empty() || _req.version.empty())
			return 400;

		if (!parse_uri())
			return 400;
		_state = HEADERS;
		_buf.erase(0, pos + 1);
	}
	return 0;
}

bool RequestParser::parse_uri() {
	size_t pos = _req.uri.find("?");
	// TODO resolve dot segments
	// Root escape check belongs to router
	_req.path = _req.uri.substr(0, pos);
	if (!(validateReqPath(_req.path)))
		return false;
	// TODO resolve percent decodes etc
	if (pos != std::string::npos) {
		_req.query = _req.uri.substr(pos + 1);
	}
	return true;

}

int RequestParser::parse_headers() {
	// need to loop as long as pos returns something
	size_t pos = _buf.find("\n");
	if (_buf[pos - 1] != '\r')
		return 400;
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

