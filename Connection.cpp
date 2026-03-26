#include <sys/epoll.h>
#include "Connection.hpp"
#include <cstring>

void Connection::enqueue_response(int epoll_fd, const std::string &response) {
	write_buf = response;
	write_offset = 0;

	// tell epoll- wake me up when this fd is writable
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLOUT;
	ev.data.ptr = &this;
	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}
