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

#include "ClientConnection.hpp"
#include "CgiConnection.hpp"
#include "Config.hpp"
#include "EpollLoop.hpp"
#include "Logger.hpp"

ClientConnection::ClientConnection(int fildes, Http *config, struct sockaddr_in addr)
	: Connection(fildes, config), listening_addr(addr),
	_parser(config, addr, &_buffer), _response(&_buffer) { }

ClientConnection::~ClientConnection() {
	if (!_cgi_tmpfile.empty())
		unlink(_cgi_tmpfile.c_str());
}

void	ClientConnection::enqueue_response() {
	// tell epoll- wake me up when this fd is writable
	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
}

void	ClientConnection::handle(uint32_t events) {
	if (events & (EPOLLERR | EPOLLHUP)) {
		EpollLoop::get_instance().del(this);
		return ;
	}
	if (events & EPOLLIN) {
// <<<<<<< HEAD
		_parser.feed(fd);
		if (_parser.getRequest().error) {
			if (_parser.getRequest().error == 500 && _parser.get_redirects() == REDIRECT_LIMIT) {
				_response.set_internal_error();
			} else {
				_response.construct(_parser.getRequest());
// =======
// 		// small buffer to test for multiple reads
// 		// TODO make it a big number!
// 		char buffer[1024];
// 		int bytes = read(fd, buffer, sizeof(buffer) - 1);
// 		if (bytes < 0) {
// 			if (errno == EAGAIN || errno == EWOULDBLOCK)
// 				return ;
// 			EpollLoop::get_instance().del(this);
// 			// TODO throw will kill the server - should wrap conn->handle
// 			// in try-catch
// 			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
// 		}
// 		if (bytes == 0) {
// 			EpollLoop::get_instance().del(this);
//
// 		} else {
// 			_parser.feed(buffer, bytes);
// 			if (_parser.getRequest().error) {
// 				std::cout << "parser error" << std::endl; // route to error page
// 				const config::errorpageinfo	&epi = _parser.getRequest().location->get_errorpages().get_page(_parser.getRequest().error);
// 				// TODO: make request accessible. We need to be able to change
// 				// the location of a response according to the path in `epi`
//
// 				// External URL
// 				if (!epi.internal) {
// 					_response.construct_3xx(epi.response_code, epi.pagename);
// 				}
//
// 				EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
// 			}
// 			if (_parser.complete()) {
// 				const Request& req = _parser.getRequest();
// 				if (!req.location->get_cgi().pass.empty()) {
// 					handle_cgi();
// 				} else {
// 					switch (req.method) {
// 						case GET: handle_get(); break ;
// 						case POST: break ;
// 						case DELETE: break ;
// 						case UNKNOWN: break ;
// 					}
// 					EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
// 				}
// >>>>>>> origin/test_framework
			}
			EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
		} else if (_parser.complete()) {
			if (!_parser.getRequest().location->get_cgi().pass.empty())
				try {
					handle_cgi();
				} catch (std::exception & e) {
					_response.set_internal_error();
				}
			else
				_response.construct(_parser.getRequest());
			EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
		}
	}
	if (events & EPOLLOUT) {
		_response.write_to(fd);
		if (_response.done() || _response.error())
			EpollLoop::get_instance().del(this);
	}
}

void	ClientConnection::handle_cgi() {

	const Request		&req		= _parser.getRequest();
	const std::string	&interp		= req.location->get_cgi().pass;
	std::string			script		= req.location->get_root() + req.path;
	// Scripts need at minimum REQUEST_METHOD, SCRIPT_FILENAME, QUERY_STRING, 
	// SERVER_PROTOCOL, GATEWAY_INTERFACE, CONTENT_TYPE/CONTENT_LENGTH for
	//  POST, plus any cgi_param pairs from the location config
	std::vector<std::string> env_strings;
	env_strings.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env_strings.push_back("SERVER_PROTOCOL=" + req.version);
	env_strings.push_back("REQUEST_METHOD=" + req.methodToString(req.method));
	env_strings.push_back("SCRIPT_FILENAME=" + req.location->get_root() + req.path);
	env_strings.push_back("PATH_INFO=" + req.path);
	env_strings.push_back("QUERY_STRING=" + req.query);
	env_strings.push_back("SERVER_NAME=" + req.hostname);

	const std::vector<std::pair<std::string, std::string> > &params =
		req.location->get_cgi().params;
	for (size_t i = 0; i < params.size(); ++i)
		env_strings.push_back(params[i].first + "=" + params[i].second);
	std::vector<char *> envp;
	for (size_t i = 0; i < env_strings.size(); ++i)
		envp.push_back(const_cast<char *>(env_strings[i].c_str()));
	envp.push_back(NULL);


	int stdout_fd[2];
	int stdin_fd[2];
	if (pipe(stdout_fd) < 0 || pipe(stdin_fd) < 0)
		return ;

	pid_t pid = fork();
	if (pid < 0) {
		close(stdout_fd[0]);
		close(stdout_fd[1]);
		close(stdin_fd[0]);
		close(stdin_fd[1]);
		return ;
	}
	

	if (pid == 0) {
		dup2(stdin_fd[0], STDIN_FILENO);
		close(stdin_fd[0]);
		close(stdin_fd[1]);
		dup2(stdout_fd[1], STDOUT_FILENO);
		close(stdout_fd[0]);
		close(stdout_fd[1]);
		char *argv[] = { (char*)interp.c_str(), (char*)script.c_str(), NULL };
		execve(interp.c_str(), argv, &envp[0]);
		exit(1);
	}
	close(stdin_fd[0]);
	close(stdout_fd[1]);
	// TODO: make loopable? states?
	// Maybe put in the RequestParser body handling or extract body handling from request parser
	if (req.method == POST && req.content_length) {
		if (_buffer.fill_capacity() > 0)
			_buffer.fill(fd);
		_buffer.feed(stdin_fd[1]);
		write(stdin_fd[1], req.body.c_str(), req.content_length);
	}
	close(stdin_fd[1]);
	CgiConnection *cgi = new CgiConnection(this);
	cgi->fd = stdout_fd[0];
	cgi->_pid = pid;
	EpollLoop::get_instance().mod(this, 0);
	EpollLoop::get_instance().add(cgi);
}

void	ClientConnection::complete_cgi(const std::string &output) {
	size_t sep = output.find("\r\n\r\n");
	if (sep == std::string::npos) {
		_response = Response(_buffer, sizeof(_buffer));
		_response.set_internal_error();
		EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
		return ;
	}
	char tmpname[] = "/tmp/cgi_XXXXXX";
	int tmpfd = mkstemp(tmpname);
	_cgi_tmpfile = tmpname;
	if (tmpfd < 0) {
		return ; // TODO 500
	}
	std::string body = output.substr(sep + 4);
	write(tmpfd, body.c_str(), body.size());
	close(tmpfd);

	_response = Response(_buffer, sizeof(_buffer));
	int status_code = 200;

	std::istringstream hstream(output.substr(0, sep));
	std::string line;
	std::vector<std::pair<std::string, std::string> > cgi_headers;
	while (std::getline(hstream, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string name = line.substr(0, colon);
		std::string value = line.substr(colon + 2);
		if (name == "Status") {
			status_code = atoi(value.c_str());
			continue;
		}
		cgi_headers.push_back(std::make_pair(name, value));
	}
	_response.add_status_line(HTTP_VERSION_STR, status_code);
	for (size_t i = 0; i < cgi_headers.size(); ++i)
		_response.add_header_field(cgi_headers[i].first, cgi_headers[i].second);
	_response.add_header_end();
	_response.set_file(tmpname);
	// TODO cleanup tmp file

	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
}
