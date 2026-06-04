#include "CgiConnection.hpp"
#include "EpollLoop.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/wait.h>

void CgiConnection::handle(uint32_t events) {
	if (events & EPOLLIN) {
		char buf[1024];
		int bytes = read(fd, buf, sizeof(buf));
		if (bytes > 0) {
			_output.append(buf, bytes);
			return ;
		}
	}
	waitpid(_pid, NULL, 0);
	_callback->complete_cgi(_output);
	EpollLoop::get_instance().del(this);

}
