#pragma once
#include <string>
#include <map>

enum HttpMethod {
	GET,
	POST,
	DELETE,
	UNKNOWN
};

struct Request {
	HttpMethod	method;
	std::string	uri;		// raw: "/site/images/42.gif?val=43"
	std::string	path;		// decoded: "/site/images/42.gif"
	std::string	query;		// querystring "val=43" or nothing
	std::string	version;	// "HTTP/1.0"
	std::map<std::string, std::string> headers; // lowercased keys
	std::string	body;
	size_t		content_length;

	std::string	host;		// extracted from headers because used frequently -
	// can maybe go
	bool		valid;		// if parsing error, false
	int			error;		// 400, 414, parsing errors

	Request() : method(UNKNOWN), content_length(0), valid(true), error(0) {}

	std::string methodToString(HttpMethod m);
	HttpMethod stringToMethod(const std::string& s);
};
