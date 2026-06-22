#include "Response.new.hpp"

#include "Logger.hpp"

#include <unistd.h>
#include <stdint.h>
#include <cstring>

#include <stdexcept>
#include <sstream>

/* RESPONSE *******************************************************************/

std::map<int, std::string>	Response::reason_phrase_map = std::map<int, std::string>();

/**	@brief Initializes the Reason Phrase map.
 */
int Response::init_reason_phrase_map() {
	try {
		reason_phrase_map.insert(std::pair<int, std::string>(200, "OK"));
		reason_phrase_map.insert(std::pair<int, std::string>(201, "Created"));
		reason_phrase_map.insert(std::pair<int, std::string>(202, "Accepted"));
		reason_phrase_map.insert(std::pair<int, std::string>(204, "No Content"));
		reason_phrase_map.insert(std::pair<int, std::string>(301, "Moved Permanently"));
		reason_phrase_map.insert(std::pair<int, std::string>(302, "Moved Temporarily"));
		reason_phrase_map.insert(std::pair<int, std::string>(304, "Not Modified"));
		reason_phrase_map.insert(std::pair<int, std::string>(400, "Bad Request"));
		reason_phrase_map.insert(std::pair<int, std::string>(401, "Unauthorized"));
		reason_phrase_map.insert(std::pair<int, std::string>(403, "Forbidden"));
		reason_phrase_map.insert(std::pair<int, std::string>(404, "Not Found"));
		reason_phrase_map.insert(std::pair<int, std::string>(405, "Method Not Allowed"));
		reason_phrase_map.insert(std::pair<int, std::string>(408, "Request Timeout"));
		reason_phrase_map.insert(std::pair<int, std::string>(413, "Content Too Large"));
		reason_phrase_map.insert(std::pair<int, std::string>(422, "Unprocessable Content"));
		reason_phrase_map.insert(std::pair<int, std::string>(500, "Internal Server Error"));
		reason_phrase_map.insert(std::pair<int, std::string>(501, "Not Implemented"));
		reason_phrase_map.insert(std::pair<int, std::string>(502, "Bad Gateway"));
		reason_phrase_map.insert(std::pair<int, std::string>(503, "Service Unavailable"));
		reason_phrase_map.insert(std::pair<int, std::string>(505, "HTTP Version Not SUpported"));
	} catch (std::exception & e) {
		LOG_ERROR("Failed to initialize Reason Phrase Map");
		return (0);
	}
	return (1);
}

/**
 *	@brief
 *	Gets the Reason Phrase associated with `code` from the reason phrase
 *	map. If none are found returns a default string.
 */
const std::string &Response::get_reason_phrase(int code) {
	static std::string unknown_status_code = std::string("Unknown Status Code");
	if (reason_phrase_map.size() == 0)
		init_reason_phrase_map();
	std::map<int, std::string>::iterator it = reason_phrase_map.find(code);
	if (it != reason_phrase_map.end())
		return (it->second);
	return (unknown_status_code);
}

Response & Response::operator=(const Response &other) {
	if (this == &other)
		return (*this);
	headers = other.headers;
	return (*this);
}

void Response::add_status_line(const std::string & version, int status_code) {
	std::ostringstream	sstream;

	sstream << version << " " << status_code << " " << get_reason_phrase(status_code) << "\r\n";
	headers.insert(headers.begin(), sstream.str());
}

void Response::add_header_field(const std::string & name, const std::string & value) {
	headers.push_back(name + ": " + value + "\r\n");
}

void Response::add_header_field(const std::string & name, size_t num) {
	std::ostringstream	iss;

	iss << num;
	headers.push_back(name + ": " + iss.str()+ "\r\n");
}

void Response::add_header_end() {
	headers.push_back("\r\n");
}

void Response::add_allowed(const Location *loc) {
	std::stringstream	stream;
	for (std::set<HttpMethod>::const_iterator it = loc->get_limit().allowed.begin();
			it != loc->get_limit().allowed.end(); it++) {
		if (it != loc->get_limit().allowed.begin())
			stream << ", ";
		stream << string_from_method(*it);
	}
	add_header_field("Allow", stream.str());
}

#include <ctime>

void Response::add_date() {
	char	buf[30];

	buf[29] = '\0';
	time_t	localtimestamp = std::time(NULL);
	// Date: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
	// "DDD, dd mm yyyy hh:mm:ss GMT"
	if (std::strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT" , std::gmtime(&localtimestamp)) == 0)
		throw (std::runtime_error("couldn't create 'Date' header string"));
	add_header_field("Date", buf);
}
