#include "Request.hpp"

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
