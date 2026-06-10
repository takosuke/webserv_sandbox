#include "CgiConnection.hpp"
#include "EpollLoop.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/wait.h>

CgiConnection::~CgiConnection() {
	// Checking for zombie processes
	if (_pid != -1)
		waitpid(_pid, NULL, 0);
}

void CgiConnection::handle(uint32_t events) {
	if (events & EPOLLIN) {
		char buf[1024];
		int bytes = read(fd, buf, sizeof(buf));
		if (bytes > 0) {
			_output.append(buf, bytes);
			return ;
		}
	}
	// Non-blocking waitpid can potentially but rarely create zombie processes
	waitpid(_pid, NULL, WNOHANG);
	_callback->complete_cgi(_output);
	EpollLoop::get_instance().del(this);
}
