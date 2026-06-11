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
#include "EpollLoop.hpp"

ClientConnection::ClientConnection(int fildes, Http *config, struct sockaddr_in addr)
	: Connection(fildes, config), listening_addr(addr),
	_parser(config, addr, &_buffer), _response(&_buffer) { }

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
		_parser.feed(fd);
		if (_parser.getRequest().error) {
			if (_parser.getRequest().error == 500 && _parser.get_redirects() == REDIRECT_LIMIT) {
				_response.set_internal_error();
			} else {
				_response.construct(_parser.getRequest());
			}
			EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
		} else if (_parser.complete()) {
			if (!_parser.getRequest().location->get_cgi().pass.empty())
				handle_cgi();
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


	int pipefd[2];
	if (pipe(pipefd) < 0)
		return ;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return ;
	}

	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		char *argv[] = { (char*)interp.c_str(), (char*)script.c_str(), NULL };
		execve(interp.c_str(), argv, &envp[0]);
		exit(1);
	}
	close(pipefd[1]);
	CgiConnection *cgi = new CgiConnection(this);
	cgi->fd = pipefd[0];
	cgi->_pid = pid;
	EpollLoop::get_instance().mod(this, 0);
	EpollLoop::get_instance().add(cgi);
}

void	ClientConnection::complete_cgi(const std::string &output) {
	size_t sep = output.find("\r\n\r\n");
	if (sep == std::string::npos) {
		return ; // TODO 500
	}
	char tmpname[] = "/tmp/cgi_XXXXXX"; // TODO random num
	int tmpfd = mkstemp(tmpname);
	if (tmpfd < 0) {
		return ; // TODO 500
	}
	std::string body = output.substr(sep + 4);
	write(tmpfd, body.c_str(), body.size());
	close(tmpfd);

	_response.add_status_line(HTTP_VERSION_STR, 200);

	std::istringstream hstream(output.substr(0, sep));
	std::string line;
	while (std::getline(hstream, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		_response.add_header_field(line.substr(0, colon), line.substr(colon + 2));
	}
	_response.add_header_end();
	_response.set_file(tmpname);
	// TODO cleanup tmp file

	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
}
