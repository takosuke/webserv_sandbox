#include "ClientConnection.new.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <algorithm>

#include "Config.hpp"
#include "EpollLoop.hpp"
#include "Logger.hpp"
#include "Response.new.hpp"
#include "ScratchBuffer.hpp"

std::string ClientConnection::_500_str = std::string("HTTP/1.0 500 Internal Server Error\r\n\r\n");

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
	: Connection(sockfd, http_conf), _state(REQ_LINE), _addr(addr) {
	_server = &(http->get_default_server(_addr));
	_buf.set_capacity(_server->get_header().buffer_size);
}

ClientConnection::~ClientConnection() {

}

void ClientConnection::handle(uint32_t events) {
	if (_state == CGI_HEADERS || _state == CGI_BODY) {
		handle_cgi_output(events);
		return;
	}
	if (events & (EPOLLERR | EPOLLHUP)) {
		EpollLoop::get_instance().del(this);
		return ;
	} else if (events & EPOLLIN) {
		if (_buf.fill_capacity() > 1)
			_buf.fill(fd);
		if (_state == REQ_LINE)
			if (!handle_req_line())
				_state = REQ_SETUP;
		if (_state == REQ_HEADERS)
			if (!handle_req_headers())
				_state = REQ_SETUP;
		if (_state == REQ_SETUP)
			if (!handle_setup())
				_state = RESPONSE;
		if (_state == REQ_BODY)
			if (!setup_cgi())
				_state = RESPONSE;
		std::cout << "BAH" << std::endl;

	


	} else if (events & EPOLLOUT) {
		// if (_state == CGI_TRANSMIT_BODY)
		// 	break ;

		// 	break ;
		// if (_state == CGI_BODY)
		// 	break ;
		if (_state == RESPONSE)
			if (handle_response())
				EpollLoop::get_instance().del(this);
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
		} else if (seg == ".." && !segments.empty()) {
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
	if (!is_digit_string(portstr) || portstr.size() > 5)
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
			std::string	req_line = std::string(_buf.data + _buf.writepos, pos);
			LOG_INFO("Request") << "incoming request: " << req_line << std::endl;
			sp1 = req_line.find(' ');
			if (sp1 == std::string::npos)
				return (_req.status = 400, false);

			std::string method = req_line.substr(0, sp1);
			if (!is_valid_token(method))
				return (_req.status = 400, false);
			_req.method = method_from_string(method);
			if (_req.method == UNKNOWN)
				return (_req.status = 501, false);

			sp2 = req_line.find(' ', sp1 + 1);
			if (sp2 == std::string::npos) {
				if (_req.method != POST)
					return (_req.status = 400, false);
				_req.version = "HTTP/0.9";
				_buf.clear();
				_state = REQ_SETUP;
			} else {
				_req.version = req_line.substr(sp2 + 1);
				if (_req.version.compare(0, 8, "HTTP/2.0") == 0)
					return (_req.status = 505, false);
				else if (!((_req.version.compare(0, 8, "HTTP/1.1") == 0
						|| _req.version.compare(0, 8, "HTTP/1.0") == 0)
					&& _req.version.find_first_not_of('0', 8) == std::string::npos))
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
				_buf.erase(0, pos + 2);
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
				_buf.erase(0, pos + 2);
				_state = REQ_SETUP;
				return (parse_req_headers());
			}
			std::transform(headers_line.begin(), headers_line.end(), headers_line.begin(), ::tolower);
			size_t colon = headers_line.find(":");
			if (!colon) {
				_state = REQ_SETUP;
				return (_req.status = 400, false);
			}
			std::string key = headers_line.substr(0, colon);
			std::string val = headers_line.substr(colon + 1);
			size_t start = val.find_first_not_of(" \t");
			if (start != std::string::npos)
				val = val.substr(start);
			// FIXME duplicate headers are being silently dropped
			_req.headers.insert(std::make_pair(key, val));
			_buf.erase(0, pos + 2);
			pos = _buf.find("\r\n");
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

bool ClientConnection::parse_req_headers() {
	std::map<std::string, std::string>::const_iterator it;

	// Parse host name
	it = _req.headers.find("host");
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
			int port = parse_portstring(portstr);
			if (port <= 0)
				return (_req.status = 400, false);
			_req.port = port;
		}
	}
	// Parse content length
	it = _req.headers.find("content-length");
	if (it != _req.headers.end()) {
		std::istringstream iss(it->second);
		iss >> _req.content_length;
		if (iss.fail())
			return (_req.status = 500, false);
	}
	else if (_req.method == POST)
		return (_req.status = 411, false); // Length required
	else
		_req.content_length = 0;
	return (true);
}

inline bool ClientConnection::is_method_allowed() {
	/* Deny POST for static content */
	if (_req.method == POST && _loc->get_cgi().is_set == false)
		return (false);
	return (_loc->get_limit().is_allowed(_req.method));
}

#include <sys/stat.h>

inline bool ClientConnection::is_file_existing() {
	std::string	path = _loc->get_root() + _req.path;
	struct stat	statbuf;

	return (stat(path.c_str(), &statbuf) == 0);
}

void ClientConnection::epi_redirect() {
	_req.method = GET;
	if (!_loc->get_errorpages().has_page(_req.status)) {
		_req.no_file = true;
		LOG_DEBUG("error_page") << "no redirection for '" << _req.path << "' found." << std::endl;
		return ;
	}
	const config::errorpageinfo &epi = _loc->get_errorpages().get_page(_req.status);
	LOG_DEBUG("error_page") << "redirection from '" << _req.path << "' to '" << epi.pagename << "'." << std::endl;
	_req.path = epi.pagename;
	if (epi.response_code != 0)
		_req.status = epi.response_code;
	if (epi.internal) {
		_req.internal = true;
		_req.no_file = false;
		_loc = &(_server->get_location(_req.path));
	} else {
		_req.internal = false;
		_req.no_file = true;
	}
}

bool ClientConnection::handle_setup() {
	int	redirects = 0;

	/* Get appropriate virtual server if a Host header field was given */
	if (!_req.hostname.empty())
		_server = &(http->get_server(_addr, _req.hostname));
	/* Default server is set up at initialization so now we can look up the
	 * Location in a loop for internal redirects.
	 * After performing a redirection we need to validate the method and
	 * existence of the file again. We perform this redirection until we hit
	 * the redirection limit. */
	_loc = &(_server->get_location(_req.path));
	while (_req.no_file == false && _req.internal == true && redirects < REDIRECT_LIMIT) {
		/* Check for existing return field in location */
		if (_loc->get_redirect().is_set) {
			const config::redirect &red = _loc->get_redirect();
			if (red.status_code)
				_req.status = red.status_code;
			LOG_DEBUG("return") << "redirection from: " << _req.path << " to " << red.path << std::endl;
			_req.path = red.path;
			/* Check if we have a url to an external file */
			if (config::starts_with_scheme(_req.path)) {
				_req.no_file = true;
				_req.internal = false;
			}
			++redirects;
		} else if (!is_method_allowed()) {
			_req.status = 405; // Method not allowed
			epi_redirect();
			++redirects;
		} else if (!is_file_existing()) {
			/* We check for file existence in all cases, since we only want
			 * file to be created through a program??? i think??? */
			_req.status = 404; // Not found
			epi_redirect();
			++redirects;
		} else {
			break ;
		}
	}
	/* Perform one last redirection if we have reached the limit and still have
	 * a valid path saved that we can check against */
	if (_req.no_file == false && _req.internal == true && redirects >= REDIRECT_LIMIT) {
		_req.status = 500;
		epi_redirect();
	}
	/* Possible states:
	 * 	a:	We found a valid file within the redirect limit
	 * 	b:	We found an external link withing the redirect limit
	 * 	c:	We did not have a file set for this error and need to return a
	 * 		Response without a body */
	/*	We want to either start initializing the response or continue to
	 *	send a body to the cgi.
	 */ 
	_state = RESPONSE;
	if (_loc->get_cgi().is_set == true) {
		if (!setup_cgi()) 
			_req.status = 500;
		return (true);
		/*
		if (setup_cgi()) {
			_state = REQ_BODY;
			return (true);
		} else {
			_req.status = 500;
			_state = RESPONSE;
		}
		*/
	}
	setup_res();
	return (true);
}

/**	@brief Sets up the response based on the information saved in `_req`.
 */ 
bool ClientConnection::setup_res() {
	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
	try {
		_res.add_status_line(HTTP_VERSION_STR, _req.status);
		if (!_req.internal)
			_res.add_header_field("Location", _req.path);
		else
			_res.add_allowed(_loc);
		_res.add_date();
		if (!_req.no_file) {
			if (set_file(_loc->get_root() + _req.path))
				_res.add_header_field("Content-Length", get_file_size());
			else {
				throw (std::runtime_error("Couldn't open stream."));
			}
		}
		_buf.clear();
	_res.add_header_end();
	} catch (std::exception &e) {
		setup_internal_error();
		return (false);
	}
	return (true);
}

void ClientConnection::buffer_res_headers() {
	while (_res.headers.size() > 0 && _buf.fill_capacity() > _res.headers.front().size()) {
		_buf.fill(_res.headers.front().c_str(), _res.headers.front().size());
		_res.headers.erase(_res.headers.begin());
	}
}

void ClientConnection::buffer_file() {
	while (_res.headers.size() == 0
		&& _stream.is_open() && _stream.good() && !_stream.eof() && _buf.fill_capacity() > 1)
		_buf.fill(_stream);
}

void ClientConnection::fill_res_buffer() {
	buffer_res_headers();
	buffer_file();
}

bool ClientConnection::setup_cgi() {
	const std::string	&interp = _loc->get_cgi().pass;
	const std::string	&script = _loc->get_root() + _req.path;
	// Scripts need at minimum REQUEST_METHOD, SCRIPT_FILENAME, QUERY_STRING, 
	// SERVER_PROTOCOL, GATEWAY_INTERFACE, CONTENT_TYPE/CONTENT_LENGTH for
	//  POST, plus any cgi_param pairs from the location config
	std::vector<std::string> env_strings;
	env_strings.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env_strings.push_back("SERVER_PROTOCOL=" + _req.version);
	env_strings.push_back("REQUEST_METHOD=" + string_from_method(_req.method));
	env_strings.push_back("SCRIPT_FILENAME=" + _loc->get_root() + _req.path);
	env_strings.push_back("PATH_INFO=" + _req.path);
	env_strings.push_back("QUERY_STRING=" + _req.query);
	env_strings.push_back("SERVER_NAME=" + _req.hostname);

	const std::vector<std::pair<std::string, std::string> > &params =
		_loc->get_cgi().params;
	for (size_t i = 0; i < params.size(); ++i)
		env_strings.push_back(params[i].first + "=" + params[i].second);
	std::vector<char *> envp;
	for (size_t i = 0; i < env_strings.size(); ++i)
		envp.push_back(const_cast<char *>(env_strings[i].c_str()));
	envp.push_back(NULL);

	/*
	int stdout_fd[2];
	int stdin_fd[2];
	if (pipe(stdout_fd) < 0) {
		if (pipe(stdin_fd) < 0) {
			close(stdout_fd[0]);
			close(stdout_fd[1]);
			return (false);
		}
		return (false);
	}
	*/
	int	stdout_fd[2];
	if (pipe(stdout_fd) < 0) {
		close(stdout_fd[0]);
		close(stdout_fd[1]);
		_req.status = 500;
		return (false);
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(stdout_fd[0]);
		close(stdout_fd[1]);
	//	close(stdin_fd[0]);
	//	close(stdin_fd[1]);
		_req.status = 500;
		return (false);
	}
	
	if (pid == 0) {
	//	dup2(stdin_fd[0], STDIN_FILENO);
	//	close(stdin_fd[0]);
	//	close(stdin_fd[1]);
		dup2(stdout_fd[1], STDOUT_FILENO);
		close(stdout_fd[0]);
		close(stdout_fd[1]);
		char *argv[] = { (char*)interp.c_str(), (char*)script.c_str(), NULL };
		execve(interp.c_str(), argv, &envp[0]);
		exit(1);
	}
//	close(stdin_fd[0]);
	close(stdout_fd[1]);
//	fcntl(stdin_fd[1], F_SETFL, O_NONBLOCK);
	// TODO: make loopable? states?
	// Maybe put in the RequestParser body handling or extract body handling from request parser
	/*
	if (_req.method == POST && _req.content_length) {
		if (_buf.fill_capacity() > 0)
			_buf.fill(fd);
		_buf.feed(stdin_fd[1]);
	}
	close(stdin_fd[1]);
	*/
	_client_fd = fd;
	_cgi_pid = pid;
	fcntl(stdout_fd[0], F_SETFL, O_NONBLOCK);
	_state = CGI_HEADERS;
	EpollLoop::get_instance().rearm(this, EPOLLIN | EPOLLERR | EPOLLHUP, stdout_fd[0]);
	return (true);
}

bool ClientConnection::handle_cgi_output(uint32_t events) {
	if (events & EPOLLIN && _buf.fill_capacity() > 1)
		_buf.fill(fd);

	if (_state == CGI_HEADERS) {
		size_t sep = _buf.find("\r\n\r\n");
		if (sep == ScratchBuffer::npos) {
			if (_buf.fill_capacity() <= 1) {
				_req.status = 500;
				finalize_cgi();
			}
			return (true);
		}
		parse_cgi_headers(sep);
		char tmpname[] = "/tmp/cgi_XXXXXX";
		int tmpfd = mkstemp(tmpname);
		close(tmpfd);
		_file = tmpname;
		_stream.open(_file.c_str(), std::ios::out | std::ios::binary);
		if (_buf.feed_capacity() > 0) {
			_buf.feed(_stream);
			_buf.clear();
		}
		_state = CGI_BODY;
	}

	if (_state == CGI_BODY) {
		if (_buf.feed_capacity() > 0) {
			_buf.feed(_stream);
			_buf.clear();
		}
	}

	if (events & EPOLLHUP)
		finalize_cgi();

	return (true);
}

void ClientConnection::parse_cgi_headers(size_t sep) {
	std::vector<std::pair<std::string, std::string> > cgi_headers;
	size_t start = 0;
	size_t end;
	_req.status = 200;
	
	while ((end = _buf.find("\r\n", start)) < sep) {
		std::string line(_buf.data + start, end - start);
		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string key = line.substr(0, colon);
			std::string val = line.substr(colon + 1);
			size_t trim = val.find_first_not_of(" \t");
			if (trim != std::string::npos)
				val = val.substr(trim);
			if (key == "Status")
				_req.status = std::atoi(val.c_str());
			else
				cgi_headers.push_back(std::make_pair(key, val));
		}
		start = end + 2;
	}

	_res.add_status_line(HTTP_VERSION_STR, _req.status);
	for (size_t i = 0; i < cgi_headers.size(); ++i)
		_res.add_header_field(cgi_headers[i].first, cgi_headers[i].second);

	_buf.erase(0, sep + 4);
}

void ClientConnection::finalize_cgi() {
	if (_buf.feed_capacity() > 0)
		_buf.feed(_stream);
	_stream.flush();
	_stream.close();
	waitpid(_cgi_pid, NULL, 0);
	_stream.open(_file.c_str(), std::ios::in | std::ios::binary);
	_res.add_header_field("Content-Length", get_file_size());
	_res.add_header_end();
	_buf.clear();
	EpollLoop::get_instance().rearm(this, EPOLLOUT | EPOLLERR | EPOLLHUP, _client_fd);
	_state = RESPONSE;
}

bool ClientConnection::set_file(const std::string &path) {
	_file = path;
	_stream.open(_file.c_str());
	return (_stream.is_open());
}

/**	@brief Returns the size of the file `_file`.
 *
 *	Throws an exception on stat() failing.
 */ 
size_t ClientConnection::get_file_size() const {
	struct stat	statbuf;
	size_t		filesize = 0;

	if (_file.size() != 0) {
		if (stat(_file.c_str(), &statbuf) != 0)
			throw (std::runtime_error("stat error"));
		filesize = statbuf.st_size;
	}
	return (filesize);
}

void ClientConnection::setup_internal_error() {
	_stream.close();
	_res.headers.clear();
	_buf.set_data(const_cast<char *>(_500_str.c_str()), _500_str.size());
	_buf.readpos = _500_str.size();
}

bool ClientConnection::handle_response() {
	if (_buf.feed_capacity() <= 0) {
		_buf.clear();
		fill_res_buffer();
	}
 	if (_res.headers.size() == 0 && (!_stream.is_open() || _stream.eof()) && _buf.feed_capacity() == 0)
		return (false);
	return (_buf.feed(fd) > 0);
}
