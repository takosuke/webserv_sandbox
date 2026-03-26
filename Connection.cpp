#include <sys/epoll.h>
#include "Connection.hpp"
#include "EpollLoop.hpp"
#include <cstring>

void Connection::enqueue_response(EpollLoop &loop, const std::string &response) {
	write_buf = response;
	write_offset = 0;
	// tell epoll- wake me up when this fd is writable
	loop.mod(this, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);
}
