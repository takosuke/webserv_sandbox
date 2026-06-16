#include "Response.hpp"

#include "Logger.hpp"

#include <unistd.h>
#include <stdint.h>
#include <cstring>

#include <stdexcept>
#include <sstream>

/* RESPONSE *******************************************************************/

std::string Response::error_buffer = std::string("HTTP/1.0 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");

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

Response::Response(ScratchBuffer *buf) {
	_buffer = buf;
	_buffer->clear();
	_error = false;
	_stream.setstate(std::ios_base::eofbit);
}

Response::Response(ScratchBuffer *buf, const std::string &filename) {
	_buffer = buf;
	_buffer->clear();
	_file = filename;
	_stream.open(filename.c_str());
	_error = !_stream.is_open();
}

Response & Response::operator=(const Response &other) {
	if (this == &other)
		return (*this);
	_buffer = other._buffer;
	_file = other._file;
	_error = other._error;
	if (_file.size() != 0) {
		_stream.open(_file.c_str());
		_error = !_stream.is_open();
	}
	return (*this);
}

bool Response::done() const {
	return (_headers.size() == 0 && (!_stream.is_open() || _stream.eof()) && _buffer->feed_capacity() == 0);
}

bool Response::error() const {
	return (_error != false || (_stream.is_open() && _stream.fail()));
}

/**	@brief Sets the streams fiel to `filename`
 * 	
 * 	error() should be checked after to see if the operation worked.
 */
void Response::set_file(const std::string &filename) {
	_file = filename;
	_stream.open(filename.c_str());
}

void Response::set_internal_error() {
	_error = false;
	_stream.close();
	_headers.clear();
	_buffer->set_data(const_cast<char *>(error_buffer.c_str()), error_buffer.size());
	_buffer->readpos = error_buffer.size();
}

void Response::add_status_line(const std::string & version, int status_code) {
	std::ostringstream	sstream;

	sstream << version << " " << status_code << " " << get_reason_phrase(status_code) << "\r\n";
	_headers.push_back(sstream.str());
}

void Response::add_header_field(const std::string & name, const std::string & value) {
	_headers.push_back(name + ": " + value + "\r\n");
}

void Response::add_header_field(const std::string & name, size_t num) {
	std::ostringstream	iss;

	iss << num;
	_headers.push_back(name + ": " + iss.str()+ "\r\n");
}

void Response::add_header_end() {
	_headers.push_back("\r\n");
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

#include <sys/stat.h>

/**	@brief Gets the content length of the file at `filepath` and adds it as a
 * 	header field.
 *
 * 	@Return 1 on completion, 0 if there was not enough room in the buffer,
 * 	-1 if a critical error occured.
 */
void Response::add_content_length() {
	struct stat	statbuf;
	size_t		filesize = 0;

	if (_file.size() != 0) {
		if (stat(_file.c_str(), &statbuf) != 0)
			throw (std::runtime_error("stat error"));
		filesize = statbuf.st_size;
	}

	std::ostringstream	stream;

	stream << filesize;
	return (add_header_field("Content-Length", stream.str()));
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

void Response::clear_buffer() {
	_buffer->clear();
}

void Response::buffer_headers() {
	while (_headers.size() > 0) {
		size_t	fillret = _buffer->fill(_headers.front().c_str(), _headers.front().size());
		if (fillret < _headers.front().size())
			break ;
		_headers.erase(_headers.begin());
	}
}

void Response::buffer_file() {
	while (_headers.size() == 0
		&& _stream.is_open() && !_stream.fail() && !_stream.eof() && _buffer->fill_capacity() > 1) {
		_buffer->fill(_stream);
	}
}

void Response::fill_buffer() {
	buffer_headers();
	buffer_file();
}

void Response::construct(const Request &req) {
	_buffer->clear();
	try {
		add_status_line(HTTP_VERSION_STR, req.error);
		if (req.error >= 300 && req.error < 400)
			add_header_field("Location", req.path);
		else
			add_allowed(req.location);
		add_date();
		if (req.method == GET) {
			if (req.path.size() != 0)
				set_file(req.location->get_root() + req.path);
			add_content_length();
		}
		add_header_end();
	}	catch (std::exception &e) {
		set_internal_error();
	}
}

void Response::write_to(int fd) {
	int	writeret;

	if (_buffer->feed_capacity() <= 0) {
		_buffer->clear();
		fill_buffer();
	}
	if (done())
		return ;
	writeret = _buffer->feed(fd);
	if (writeret < 0)
		_error = true;
}
