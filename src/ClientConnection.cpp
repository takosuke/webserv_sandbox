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
				if (!_parser.getRequest().location->get_cgi().pass.empty())
					handle_cgi();
				else {
					switch (_parser.getRequest().method) {
						case GET: handle_get(); break ;
						case POST: break ;
						case DELETE: break ;
						case UNKNOWN: break ;
					}
				}
			}
		}
	}
	if (events & EPOLLOUT) {
		// const char *data = _write_buf.c_str() + _write_offset;
		// size_t remaining = _write_buf.size() - _write_offset;
		//
		// int sent = write(fd, data, remaining);
		//
		// if (sent < 0) {
		// 	if (errno == EAGAIN || errno == EWOULDBLOCK)
		// 		return ;
		// 	EpollLoop::get_instance().del(this);
		// } else {
		// 	_write_offset += sent;
		// 	if (_write_offset == _write_buf.size()) {
		// 		_write_buf.clear();
		// 		_write_offset = 0;
		// 		EpollLoop::get_instance().mod(this, EPOLLIN | EPOLLERR | EPOLLHUP);
		// 	}
		// }
		
		_response.write_to(fd);
		if (_response.done() || _response.error())
			EpollLoop::get_instance().del(this);
	}
}

void	ClientConnection::handle_get() {
	// const Request &	req = _parser.getRequest();
	// _fileconnection = new ReadFileConnection(ClientCallback(this, EPOLLOUT | EPOLLERR | EPOLLHUP));
	// if (_fileconnection == NULL) {
	// 	_resstream.response(*(ResponseCache::get_instance().get(
	// 		req.location->get_errorpages().get_page(500).page)));
	// 	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
	// } else {
	// 	_fileconnection->open_file(req.location->get_root() + req.path);
	// 	if (_fileconnection->state != 0) {
	// 		_resstream.response(*(ResponseCache::get_instance().get(
	// 			req.location->get_errorpages().get_page(_fileconnection->state).page)));
	// 		EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
	// 		return ;
	// 	}
	// 	_fileconnection->set_operation_size(BYTES_PER_READ_CYCLE);
	// 	EpollLoop::get_instance().mod(this, 0);
	// 	FileLoop::get_instance().add(_fileconnection);
	// }
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
