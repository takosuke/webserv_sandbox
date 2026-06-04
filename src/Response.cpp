#include "Response.hpp"

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

Response::Response(char *buf, size_t buffer_size) {
	buffer = buf;
	_error = false;
	stream.setstate(std::ios_base::eofbit);
	pos = 0;
	size = 0;
	capacity = buffer_size;
}

Response::Response(char *buf, size_t buffer_size, const std::string &filename) {
	buffer = buf;
	stream.open(filename.c_str());
	_error = !stream.is_open();
	pos = 0;
	size = 0;
	capacity = buffer_size;
}

bool Response::done() const {
	return (headers.size() == 0 && stream.eof() && pos >= size);
}

bool Response::error() const {
	return (_error != false || stream.fail());
}

/**	@brief Sets the streams fiel to `filename`
 * 	
 * 	error() should be checked after to see if the operation worked.
 */
void Response::set_file(const std::string &filename) {
	stream.open(filename.c_str());
}

void Response::add_status_line(const std::string & version, int status_code) {
	std::ostringstream	sstream;

	sstream << version << " " << status_code << get_reason_phrase(status_code) << "\r\n";
	headers.push_back(sstream.str());
}

void Response::add_header_field(const std::string & name, const std::string & value) {
	headers.push_back(name + ": " + value + "\r\n");
}

void Response::add_header_end() {
	headers.push_back("\r\n");
}

#include <sys/stat.h>

/**	@brief Gets the content length of the file at `filepath` and adds it as a
 * 	header field.
 *
 * 	@Return 1 on completion, 0 if there was not enough room in the buffer,
 * 	-1 if a critical error occured.
 */
void Response::add_content_length(const std::string &filepath) {
	struct stat	statbuf;

	if (stat(filepath.c_str(), &statbuf) != 0)
		throw (std::runtime_error("stat error"));

	std::ostringstream	stream;

	stream << statbuf.st_size;
	return (add_header_field("Content-Length", stream.str()));
}

#include <ctime>

void Response::add_date() {
	char	buf[29];

	buf[28] = '\0';
	time_t	localtimestamp = std::time(NULL);
	// Date: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
	// "DDD, dd mm yyyy hh:mm:ss GMT"
	if (std::strftime(buf, 29, "%a, %d %b %y %H:%M%S GMT" , std::gmtime(&localtimestamp)))
		throw (std::runtime_error("couldn't create 'Date' header string"));
	add_header_field("Date", buf);
}

void Response::clear_buffer() {
	size = 0;
	pos = 0;
}

void Response::buffer_headers() {
	while (headers.size() > 0 && headers.front().size() <= capacity - size) {
		std::strcpy(buffer + size, headers.front().c_str());
		size += headers.front().size();
		headers.erase(headers.begin());
	}
}

void Response::buffer_file() {
	if (stream.is_open() && !stream.fail() && !stream.eof()) {
		std::ios::pos_type	prev = stream.tellg();

		stream.get(buffer + size, capacity - size);
		size += stream.tellg() - prev;
	}
}

void	Response::fill_buffer() {
	buffer_headers();
	buffer_file();
}

int	Response::construct_3xx(int code, const std::string &location) {
	clear_buffer();
	try {
		add_status_line(HTTP_VERSION_STR, code);
		add_header_field("Location", location);
		add_date();
		add_header_end();
	}	catch (std::exception &e) {
		return (0);
	}
	return (1);
}

void Response::write_to(int fd) {
	int	writeret;

	if (pos >= size) {
		clear_buffer();
		fill_buffer();
	}
	if (done())
		return ;
	writeret = write(fd, buffer + pos, size - pos);
	if (writeret < 0)
		_error = true;
	pos += writeret;
}
