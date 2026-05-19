#include "RequestParser.hpp"
#include <cctype>
#include <algorithm>
#include "Logger.hpp"
#include "Request.hpp"

// TODO
// max uri length?
// better error resolution for normalizeRequestPath
// general cleanup

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
	return std::isxdigit(static_cast<unsigned char>(a)) 
	&& std::isxdigit(static_cast<unsigned char>(b));
}

static bool isDigitString(const std::string& s) {
	if (s.empty()) return false;
	for (size_t i = 0; i < s.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
	}
	return true;
}

static bool isValidURLEncoding(std::string url) {

	size_t pos = url.find('%');
	while (pos != std::string::npos) {
		if (pos + 2 >= url.size())
			return false;
		if (!isValidHex(url[pos + 1], url[pos + 2]))
			return false;
		url.erase(0, pos + 1);
		pos = url.find('%');
	}
	return true;
}

static int validateVersion(const std::string& version) {
	// TODO accept HTTP/1.000 and HTTP/1.1000?
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

static std::string normalizeReqPath(const std::string& path) {
	std::vector<std::string> segments;
	std::istringstream ss(path);
	std::string seg;

	while (std::getline(ss, seg, '/')) {
		if (seg == "." || seg.empty()) {
			continue;
		} else if (seg == "..") {
			if (!segments.empty())
				segments.pop_back();
			else 
				return ""; // TODO ugh better error checking
		} else {
			segments.push_back(seg);
		}
	}

	std::string result = "/";
	for (size_t i = 0; i < segments.size(); ++i) {
		result += segments[i];
		if (i + 1 < segments.size())
			result += "/";
	}
	return result;
}

static bool validateReqPath(const std::string& path) {
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
	if (_buf.find("\r\n") == std::string::npos && _state != BODY)
		return ;
	if (_state == REQUEST_LINE && parse_request_line() != 0)
		return ;
	if (_state == HEADERS)
		parse_headers();
	if (_state == BODY) {
		if (_req.server == NULL && _http != NULL) {
			_req.server = &(_http->get_server(_addr, getRequest().host));
			_req.location = &(_req.server->get_location(getRequest().path));
		}
		parse_body();
	}
	if (_state == COMPLETE || _req.error)
	{
		LOG_DEBUG("REQUEST") << "Request:" << _req << std::endl;
		// TODO remove this debugging
		_req.printRequest();
		return;
	}
}

int RequestParser::parse_request_line() {
	size_t pos = _buf.find("\r\n");
	if (pos != std::string::npos)
	{
		std::string request_line = _buf.substr(0, pos);
		size_t sp_first = request_line.find(' ');
		size_t sp_second = request_line.find(' ', sp_first + 1);
		if (sp_first == std::string::npos)
			return (set_error(400));
		// does this return npos for http/0.9
		if (request_line.find(' ', sp_second + 1) != std::string::npos)
			return (set_error(400));
		std::string method = request_line.substr(0, sp_first);
		if (!isValidToken(method))
			return (set_error(400));
		_req.method = _req.stringToMethod(method);
		if (_req.method == UNKNOWN)
			return (set_error(501));
		_req.uri = request_line.substr(sp_first + 1, sp_second - sp_first - 1);
		if (_req.uri.empty() || !isValidURLEncoding(_req.uri))
			return (set_error(400));
		if (!parse_uri())
			return (set_error(400));
		// TODO this feels brittle, check that this won't fuck up
		// if the request line only has 2 tokens and they are a valid method and
		// a valid URI, then it's HTTP/0.9 and the request is complete
		if (sp_second == std::string::npos) {
			_req.version = "HTTP/0.9";
			_state = COMPLETE;
			_complete = true;
			_buf.erase();

			return 0;
		}
		else {
			_req.version = request_line.substr(sp_second + 1);
			// returns 0 if valid
			int isNotValidHttpVersionErrno = validateVersion(_req.version);
			if (isNotValidHttpVersionErrno)
				return (set_error(isNotValidHttpVersionErrno));
		}
		_state = HEADERS;
		_buf.erase(0, pos + 2);
	}
	return 0;
}

bool RequestParser::parse_uri() {
	size_t pos = _req.uri.find("?");
	// TODO make this return an error rather than an empty string?
	_req.path = normalizeReqPath(_req.uri.substr(0, pos));
	if (!(validateReqPath(_req.path)) || _req.path == "") // TODO I don't like
		return false;

	// query string sent as is to CGI handler who is supposed to do the percent
	// decoding etc
	if (pos != std::string::npos) {
		_req.query = _req.uri.substr(pos + 1);
	}
	return true;
}

int RequestParser::parse_headers() {
	size_t pos = _buf.find("\r\n");
	while (pos != std::string::npos) {
		// TODO max number of headers/max header size?
		std::string headers_line = _buf.substr(0, pos);
		if (headers_line.empty())
		{
			_buf.erase(0, pos + 2);
			_state = BODY;
			parse_content_length();
			parse_hostname();
			return 0;
		}
		// make headers into lowercase for consistency and avoiding headaches
		// FIXME header value trailing whitespace not trimmed
		std::transform(headers_line.begin(), headers_line.end(), headers_line.begin(), ::tolower);
		size_t colon = headers_line.find(":");
		if (!colon)
			return (set_error(400));
		std::string key = headers_line.substr(0, colon);
		std::string val = headers_line.substr(colon + 1);
		size_t start = val.find_first_not_of(" \t");
		if (start != std::string::npos)
			val = val.substr(start);
		// FIXME duplicate headers are being silently dropped
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

void RequestParser::parse_hostname() {
	std::map<std::string, std::string>::iterator it = _req.headers.find("host");
	if (it != _req.headers.end()) {
		std::istringstream iss(it->second);
		iss >> _req.host;
	}
	if (!_req.host.empty()) {
		std::string::size_type colon = _req.host.rfind(':');
		if (colon == std::string::npos) {
			_req.hostname = _req.host; 
			_req.port = -1;
		} else {
			_req.hostname = _req.host.substr(0, colon);
			std::string portstring = _req.host.substr(colon + 1);
			if (_req.hostname.empty() || portstring.empty()) {
				_req.error = 400;
				return ;
			}
			int port = parse_portstring(portstring);
			if (port == -1) {
				_req.error = 400;
				return ;
			}
			_req.port = port;
		}
	}
}

int RequestParser::parse_portstring(std::string portstring) {
	if (!isDigitString(portstring))
		return -1;
	
	if (portstring.size() > 5)
		return -1;

	int port = std::atoi(portstring.c_str());
	if (port < 1 || port > 65535)
		return -1;

	return port;
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
