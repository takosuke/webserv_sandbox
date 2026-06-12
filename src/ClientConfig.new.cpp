#include "ClientConnection.new.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

#include "CgiConnection.hpp"
#include "Config.hpp"
#include "EpollLoop.hpp"
#include "Logger.hpp"

/** The Client Connection is responsible for handling most of the actuall data
 * transfer and interpretation of said data.
 *
 * This happens in certain stages, that are imposed by the availability of new
 * data from the client and what we can do with this information.
 *
 * In the following list of stages optional stages aremarked with a asterisk.
 *
 * The stages a Connection can be in are:
 * - Reading the request line & request headers
 * - * Opening a file or cgi connection
 * - * Transmitting data to a file from the client
 * - Sending the response status line and response headers
 * - * Transmitting data from a file to the client
 *
 * In between these steps certain things need to be set up that can reult in
 * errors, like handling internal redirections, etc which can lead to certain
 * stages being skipped.
 *
 * # Reading the request line & request headers
 *
 * Once a new Connection is established, the Connection get's called to be
 * handled whenever new data arrives on it's socket.
 *
 * First the status line is interpreted and the important parts like the method
 * and the uri are saved.
 * 
 * Based on the Method we can set the Requests status code to the appropriate
 * Successfull Response (200 for GET, 201 for POST, 200 for DELETE)
 *
 * After that the headers are interpreted until an empty line with the sequence
 * '\r\n' is encountered, marking the end of the headers.
 *
 * With a given Host header field we can set the Server and Location this
 * request is for. If no Host field is included we can use the default server
 * and it's appropriate Location.
 *
 * Once this is done we can validate:
 * 1. That the requested method is allowed for this Location
 * 2. That the requested file exists (GET) or the directory exists (POST)
 * 3. That there is a Content-Length (POST)
 *
 * If any of these checks don't succeed we set up the appriopriate error and
 * perform a redirection based on the saved paths or urls from the errorpages
 * for this Location.
 *
 * ## Redirections
 *
 * A redirection consists mostly of setting up a different path in the request
 * for the requested resource with the request Method changed to GET.
 *
 * We need to perform the same checks as for the original request on the changed
 * path and stop the internal redirections in case they result in a loop. For
 * this we can set up a hard limit of maximum redirections and try to perform
 * one last redirection with a 500 (Internal Server Error) status code.
 *
 * If a redirection ends in the maximum amount of redirections reached and a
 * non existing file we can answer with an empty 500 Response that does not
 * send body.
 *
 * # Opening a file or cgi connection
 *
 * In case of a POST request or a request to a cgi-pass Location, we need to
 * set up the appropriate connection to these. 
 *
 * # Transmitting data to a file from the client
 *
 * While we still recieve new data from the client we can continously send it to
 * the previously opened connection. We just write fromt the buffer if there is
 * data available, reset the buffer if all available data is written and after
 * that fill it from the client. This way we are continously called until a
 * client doesn't have data to send anymore.
 *
 * For files we could also immediatly write new data to the file since the
 * connection is not limited.
 *
 * In case of a cgi request the cgi might have not recieved all the data that
 * we want to send once the client has sent all their data. For this situation
 * we need a special flag to inform our program of the cgi having recieved all
 * data so we can perform a switch from writing to the cgi to reading from it.
 */

ClientConnection::ClientConnection(int sockfd, Http *http_conf, struct sockaddr_in addr)
	: Connection(sockfd, http_config), _state(REQ_LINE), _addr(addr) {
	_server = &(http->get_default_server(_addr));
	_buf.set_capacity(_server->get_headers().buffer_size);
}

ClientConnection::~ClientConnection() {

}

void ClientConnection::handle(uint32_t events) {
	if (events & (EPOLLERR | EPOLLHUP)) {
		EpollLoop.get_instance().del(this);
		return ;
	} else if (events & EPOLLIN) {
		_buffer.fill(fd);
		if (_state == REQ_LINE)
			if (!handle_req_line())
				_state = REQ_SETUP;
		if (_state == REQ_HEADERS)
			if (!handle_req_headers())
				_state = REQ_SETUP;
		if (_state == REQ_SETUP)
			if (!handle_setup())
				???;
		if (_state == REQ_BODY)
			break ;
	} else if (events & EPOLLOUT) {
		if (_state == CGI_TRANSMIT_BODY)
			break ;
		if (_state == CGI_HEADERS)
			break ;
		if (_state == CGI_BODY)
			break ;
		if (_state == RES_HEADERS)
			break ;
		if (_state == RES_BODY)
			break ;
	}
}

/* HELPER FUNCTIONS ***********************************************************/

static bool is_token_char(unsigned char c) {
	static const std::string separators = "()<>@,;:\\\"/[]?={} \t";

	if (c >= 127)	return false;
	if (c <= 31)	return false;
	return separators.find(static_cast<char>(c)) == std::string::npos;
}

static bool is_valid_token(const std::string& token) {
	if (token.empty()) return false;
	for (size_t i = 0; i < token.size(); ++i)
		if (!is_token_char(static_cast<unsigned char>(token[i])))
			return false;
	return true;
}

static bool is_uri_token_char(unsigned char c) {
	if (c >= 127)	return false;
	if (c <= 32)	return false;
	return true;
}

static bool is_valid_hex(char a, char b) {
	return std::isxdigit(static_cast<unsigned char>(a)) 
	&& std::isxdigit(static_cast<unsigned char>(b));
}

static bool is_digit_string(const std::string& s) {
	if (s.empty()) return false;
	for (size_t i = 0; i < s.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
	}
	return true;
}

static bool is_valid_url_encoding(std::string url) {

	size_t pos = url.find('%');
	while (pos != std::string::npos) {
		if (pos + 2 >= url.size())
			return false;
		if (!is_valid_hex(url[pos + 1], url[pos + 2]))
			return false;
		url.erase(0, pos + 1);
		pos = url.find('%');
	}
	return true;
}

static std::string normalize_req_path(const std::string& path) {
	std::vector<std::string> segments;
	std::istringstream ss(path);
	std::string seg;

	while (std::getline(ss, seg, '/')) {
		if (seg == "." || seg.empty()) {
			continue;
		} else if (seg == ".." && !sements.empty()) {
			segments.pop_back();
			// I don't think we care if there are any ".." instances that would
			// move us above the root. We can just disregard them and stay in
			// "/" until we hit another valid segment
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

static bool validate_req_path(const std::string& path) {
	if (path.empty())
		return false;
	for (size_t i = 0; i < path.size(); ++i)
		if (!is_uri_token_char(static_cast<unsigned char>(path[i])))
			return false;
	for (size_t i = 0; i < path.size(); ++i) {
		if (path[i] == '%') {
			if (i + 2 >= path.size())
				return false;
			if (!is_valid_hex(path[i + 1], path[i + 2]))
				return false;
		}
	}
	return true;
}

static int parse_portstring(const std::string &portstr) {
	if (!is_digit_sring(portstr) || portstr.size() > 5)
		return (-1);

	int	port = std::atoi(portstr.c_str());
	if (port < 1 || port > 65535)
		return (-1);
	return (port);
}

/**	@brief Checks if a full request line is present and parses it. Once the
 *  parsing is complete set's up the next state.
 *
 *  @return Returns false if an error occurred. Returns true on successfull or
 *  incomplete req_line.
 */ 
bool	ClientConnection::handle_req_line() {
	size_t	pos = _buf.find("\r\n");
	if (pos != ScratchBuffer::npos) {
		try {
			size_t	sp1, sp2;
			std::string	req_line = std::string(_buffer->data + _buffer->write_pos, pos);
			LOG_INFO("Request") << "incoming request: " << req_line << std::endl;
			sp1 = req_line.find(' ');
			if (sp1 == std::string::npos)
				return (_req.status = 400, false);

			std::string method = req_line.substr(0, sp1);
			if (!is_valid_token(method))
				return (_req.status = 400, false);
			_req.method = _req.method_from_string(method);
			if (_req.method == UNKOWN)
				return (_req.status = 501, false);

			sp2 = req_line.find(' ', sp1 + 1);
			if (sp2 == std::string::npos) {
				if (_req.method != POST)
					return (_req.status == 400, false);
				_req.version = "HTTP/0.9";
				_buf->clear();
				_state = REQ_SETUP;
			} else {
				_req.version = req_line.substr(sp1 + 1);
				if (_req.version.compare(0, 8, "HTTP/2.0"))
					return (_req.status = 505, false);
				else if (!((_req.version.compare(0, 8, "HTTP/1.1") == 0
						|| _req.version.compare(0, 8, "HTTP/1.0") == 0)
					&& _req.version.find_first_not_of('0', 8) != std::string::npos)))
					return (_req.status = 400, false);

				_req.uri = req_line.substr(sp1 + 1, sp2 - sp1 - 1);
				if (_req.uri.empty() || !is_valid_url_encoding(_req.uri))
					return (_req.status = 400, false);				
				size_t	query_start = _req.uri.find("?");

				_req.path = normalize_req_path(_req.uri.substr(0, query_start));
				if (!validate_req_path(_req.path))
					return (_req.status = 400, false);
				if (query_start != std::string::npos)
					_req.query = _req.uri.substr(query_start + 1);
				_buffer->erase(0, pos + 2);
				_state = REQ_HEADERS;
			}
		} catch ( std::exception &e ) {
			LOG_WARN("Request") << "caugth exception: " << e.what() << std::endl;
			return (_req.status = 500, false);
		}
	} else if (_buf.fill_capacity() <= 0) {
		_req.status = 414; // URI too large
		return (false);
	}
	return (true);
}

/**	@brief Checks if any amount of request headers are present and parses them.
 *	Once the parsing is complete, set's up the next state.
 *
 *  @return Returns false if an error occurred. Returns true on successfull
 *  parsing or if no header line end was encountered.
 */ 
bool	ClientConnection::handle_req_headers() {
	size_t	pos = _buf.find("\r\n");
	while (pos != ScratchBuffer::npos) {
		try {
			std::string	headers_line = std::string(_buf.data + _buf.writepos, pos);
			if (headers_line.empty()) {
				buf.erease(0, pos + 2);
				_state = REQ_SETUP;
				return (parse_headers());
			}
		} catch ( std::exception & e) {
			LOG_WARN("Request") << "caugth exception: " << e.what() << std::endl;
			return (_req.status = 500, false);
		}
	}
	if (_buf.fill_capacity() <= 0) {
		return (_req.status = 431, false); // Request header fields too large
	}
	return (true);
}

bool ClientConnection::parser_headers() {
	// Parse host name
	std::map<std::string, std::string>const_iterator it = _req.headers.find("host");
	if (it != _req.headers.end())
		_req.host = it->second;
	if (!_req.host.empty()) {
		std::string::size_type	colon = _req.host.rfind(':');
		if (colon == std::string::npos) {
			_req.hostname = _req.host;
			_req.port = -1;
		} else {
			_req.hostname = _req.host.substr(0, colon);
			std::string	portstr = _req.host.substr(colon + 1);
			if (_req.hostname.empty() || portstr.empty())
				return (_req.status = 400, false);
			int port = parse_portstring(portstring);
			if (port <= 0)
				return (_req.status = 400, false);
			_req.port = port;
		}
	}
	// Parse content length
	std::map<std::string, std::string>::const_iterator it = _req.headers.find("content-length");
	if (it != _req.headers.end()) {
		std::istringstream iss(it->second);
		iss >> _req.content_length;
		if (iss.fail())
			return (_req.state = 500, false);
	}
	else if (_req.method == POST)
		return (_req.state = 411, false); // Length required
	else
		_req.content_length = 0;
	return (true);
}
