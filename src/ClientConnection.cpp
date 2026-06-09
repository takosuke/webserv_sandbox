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
#include "Logger.hpp"

ClientConnection::~ClientConnection() {

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
		// small buffer to test for multiple reads
		// TODO make it a big number!
		char buffer[1024];
		int bytes = read(fd, buffer, sizeof(buffer) - 1);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			EpollLoop::get_instance().del(this);
			// TODO throw will kill the server - should wrap conn->handle
			// in try-catch
			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
		}
		if (bytes == 0) {
			EpollLoop::get_instance().del(this);

		} else {
			_parser.feed(buffer, bytes);
			if (_parser.getRequest().error) {
				std::cout << "parser error" << std::endl; // route to error page
				const config::errorpageinfo	&epi = _parser.getRequest().location->get_errorpages().get_page(_parser.getRequest().error);
				// TODO: make request accessible. We need to be able to change
				// the location of a response according to the path in `epi`
				
				// External URL
				if (!epi.internal) {
					_response.construct_3xx(epi.response_code, epi.pagename);
				}
				
				EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
			}
			if (_parser.complete()) {
				const Request& req = _parser.getRequest();
				if (!req.location->get_cgi().pass.empty()) {
					handle_cgi();
				} else {
					switch (req.method) {
						case GET: handle_get(); break ;
						case POST: break ;
						case DELETE: break ;
						case UNKNOWN: break ;
					}
					EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
				}
			}
		}
	}
	if (events & EPOLLOUT) {
		_response.write_to(fd);
		if (_response.done() || _response.error())
			EpollLoop::get_instance().del(this);
	}
}

void	ClientConnection::handle_get() {
	const Request &	req = _parser.getRequest();
	_response = Response(_buffer, 1024, req.location->get_root() + req.path);
	if (_response.error()) {
		_response.set_internal_error();
		return ;
	}
	try {
		_response.add_status_line(HTTP_VERSION_STR, 200);
		_response.add_content_length();
		_response.add_date();
		{
			std::stringstream	stream;
			for (std::set<config::limit::method>::const_iterator it = req.location->get_limit().allowed.begin();
			it != req.location->get_limit().allowed.end(); it++) {
				if (it != req.location->get_limit().allowed.begin())
					stream << ", ";
				stream << config::limit::string_from_method(*it);
			}
			_response.add_header_field("Allow", stream.str());
		}
		_response.add_header_end();
	} catch (std::exception &e) {
		_response.set_internal_error();
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
	if (req.method == POST && req.content_length) {
		LOG_DEBUG("stdin") << req.body << std::endl;
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

	_response = Response(_buffer, sizeof(_buffer));
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
