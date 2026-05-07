#include "RequestParser.hpp"
#include <cctype>
#include <algorithm>
#include "Logger.hpp"
#include "Request.hpp"


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
		if (!isValidMethodString(method))
			return 400;
		_req.method = _req.stringToMethod(method);
		if (_req.method == UNKNOWN)
			return 501;
		_req.uri = request_line.substr(sp_first + 1, sp_second - sp_first - 1);
		_req.version = request_line.substr(sp_second + 1);
		if (_req.uri.empty() || _req.version.empty())
			return 400;

		parse_uri();
		_state = HEADERS;
		_buf.erase(0, pos + 1);
	}
	return 0;
}

void RequestParser::parse_uri() {
	size_t pos = _req.uri.find("?");
	_req.path = _req.uri.substr(0, pos);
	if (pos != std::string::npos) {
		_req.query = _req.uri.substr(pos + 1);
	}

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
		return 0;
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

bool RequestParser::isValidMethodString(const std::string& method) {
	if (method.empty()) return false;
	static const std::string separators = "()<>@,;:\\\"/[]?={} \t";
	for (size_t i = 0; i < method.size(); ++i)
	{
		if (method[i] >= 127) return false;
		if (method[i] <= 31) return false;
		if (separators.find(static_cast<char>(method[i])) != std::string::npos)
			return false;
	}
	return true;
}

