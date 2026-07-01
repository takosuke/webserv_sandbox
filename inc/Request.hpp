#pragma once

#include "Config.hpp"

#include <string>

struct Request {
	Request();

	// TODO beginning underscores for consistency?
	HttpMethod	method;
	std::string	uri;		// raw: "http://site/images/42.gif?val=43"
	std::string	path;		// decoded: "images/42.gif"
	std::string	query;		// querystring "val=43" or nothing
	std::string	version;	// "HTTP/1.0"
	std::map<std::string, std::string> headers; // lowercased ke=ys
	size_t		content_length;

	std::string	host;		// extracted from headers because used frequently -
	std::string	hostname;
	int			port;

	int			status;		// current status code fo the request (200, 201, 404, ...)
	
	bool		internal;	// tells if a path is internal or an external URL
	bool		no_file;	// tells if we need to construct a body less response
};
