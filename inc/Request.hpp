#pragma once

#include "Config.hpp"

#include <string>
#include <iostream>
#include <map>

enum HttpMethod {
	GET,
	POST,
	DELETE,
	UNKNOWN
};

struct Request {
	// TODO beginning underscores for consistency?
	HttpMethod	method;
	std::string	uri;		// raw: "http://site/images/42.gif?val=43"
	std::string	path;		// decoded: "images/42.gif"
	std::string	query;		// querystring "val=43" or nothing
	std::string	version;	// "HTTP/1.0"
	std::map<std::string, std::string> headers; // lowercased ke=ys
	std::string	body;
	size_t		content_length;

	std::string	host;		// extracted from headers because used frequently -
	std::string	hostname;
	int			port;

	const Server *		server;
	const Location *	location;

	// can maybe go
	bool		valid;		// if parsing error, false
	int			error;		// 400, 414, parsing errors

	// TODO destructors etc
	Request() : method(UNKNOWN), content_length(0), valid(true), error(0), server(NULL), location(NULL) {}

	std::string methodToString(HttpMethod m);
	HttpMethod stringToMethod(const std::string& s);
	void		printRequest();
	friend std::ostream& operator<<(std::ostream& os, const Request& req) {
		os << "Request method:" << req.method << std::endl
			<< "Request path:" << req.path << std::endl
			<< "Query string:" << req.query << std::endl;
		return os;
	}
};
