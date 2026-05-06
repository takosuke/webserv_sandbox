#include "Request.hpp"
#include <iostream>

std::string Request::methodToString(HttpMethod m) {
	switch (m) {
		case GET:		return "GET";
		case POST:		return "POST";
		case DELETE:	return "DELETE";
		default:		return "UNKNOWN";
	}
}

HttpMethod Request::stringToMethod(const std::string& s) {
	if (s == "GET")		return GET;
	if (s == "POST")	return POST;
	if (s == "DELETE")	return DELETE;
	return UNKNOWN;
}

void	Request::printRequest() {
	std::cout << "=== Full Parsed Request ===" << std::endl;
	std::cout << "Request method:" << method << std::endl;
	std::cout << "Request path:" << path << std::endl;
	std::cout << "Query String:" << query << std::endl;
	std::cout << "Http version:" << version << std::endl;
	std::cout << "Headers:" << std::endl;
	for (std::map<std::string, std::string>::iterator it = headers.begin(); it != headers.end(); ++it)
		std::cout << it->first << ":" << it->second << std::endl;
	std::cout << "content-length:" << content_length << std::endl;
	std::cout << "Body:" << std::endl;
	std::cout << body << std::endl;
	std::cout << "=== End Request ===" << std::endl;
}
