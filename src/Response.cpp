#include "Response.hpp"

#include "Logger.hpp"

#include <unistd.h>
#include <stdint.h>

#include <stdexcept>
#include <sstream>

std::map<int, std::string>	Response::reason_phrase_map = std::map<int, std::string>();

/**	@brief Initializes the Reason Phrase map.
 */
void Response::init_reason_phrase_map() {
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
	}
}

/**
 *	@brief
 *	Gets the Reason Phrase associated with `code` from the reason phrase
 *	map. If none are found returns a default string.
 */
std::string Response::get_reason_phrase(int code) {
	if (reason_phrase_map.size() == 0)
		init_reason_phrase_map();
	std::map<int, std::string>::iterator it = reason_phrase_map.find(code);
	if (it != reason_phrase_map.end())
		return (it->second);
	return ("Unknown Status Code");
}

void Response::construct_status_line(const std::string & version, int status_code) {
	std::ostringstream	in;

	in << version << " "
		<< status_code << " "
		<< get_reason_phrase(status_code) << "\r\n";
	status_line = in.str();
}

void Response::add_header_field(const std::string & name, const std::string & value) {
	if (headers.size() != 0)
		headers.erase(headers.size() - 1);
	headers.reserve(headers.size() + name.size() + value.size() + 4);
	headers.append(name);
	headers.append(": ");
	headers.append(value);
	headers.append("\r\n\r\n");
}

/**
 *	@brief
 *	Writes `count` bytes of the response to `fd`.
 *
 * 	@return
 * 	> 0 on succesfull write, < 0 on error, 0 if the end of the response
 * 	was reached.
 */
int	Response::write_count(int fd, size_t count) {
	const char	*data = _writebuf->c_str() + _writepos;
	size_t		remaining = std::min(count, _writebuf->size() - _writepos);

	int sent = write(fd, data, remaining);
	if (sent < 0)
		return (-1);

	count -= sent;
	_writepos += sent;
	while (_writepos == _writebuf->size()) {
		if (reinterpret_cast<uintptr_t>(_writebuf) == reinterpret_cast<uintptr_t>(&status_line))
			_writebuf = &headers;
		else if (reinterpret_cast<uintptr_t>(_writebuf) == reinterpret_cast<uintptr_t>(&headers))
			_writebuf = &entity;
		else
			return (0);
		_writepos = 0;
	}
	return (1);
}
