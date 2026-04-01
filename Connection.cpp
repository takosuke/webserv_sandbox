#include <cerrno>
#include <sys/epoll.h>
#include "Connection.hpp"
#include "EpollLoop.hpp"
#include <cstring>
#include <unistd.h>
#include <stdexcept>

void Connection::enqueue_response(EpollLoop &loop, const std::string &response) {
	write_buf = response;
	write_offset = 0;
	// tell epoll- wake me up when this fd is writable
	loop.mod(this, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);
}

void ClientConnection::handle(EpollLoop &loop, uint32_t events) {
	if (events & EPOLLIN) {
		char buffer[4096];
		int bytes = read(fd, buffer, sizeof(buffer) - 1);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
		}
		if (bytes == 0) {
			loop.del(this);
		} else {
			buffer[bytes] = '\0';
			std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
			enqueue_response(loop, response);
		}
	}
	if (events & EPOLLOUT) {
		const char *data = write_buf.c_str() + write_offset;
		size_t remaining = write_buf.size() - write_offset;

		int sent = write(fd, data, remaining);

		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			loop.del(this);
		} else {
			write_offset += sent;
			if (write_offset == write_buf.size()) {
				write_buf.clear();
				write_offset = 0;
				loop.mod(this, EPOLLIN | EPOLLERR | EPOLLHUP);
			}
		}
	}
}
