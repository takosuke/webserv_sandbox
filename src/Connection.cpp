#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
#include <stdexcept>
#include "Connection.hpp"
#include "EpollLoop.hpp"
#include "utils.hpp"

void	ServerConnection::handle(EpollLoop &loop, uint32_t events) {

	(void)events;
	sockaddr_in client_addr;
	socklen_t	client_len = sizeof(client_addr);
	int client_fd = accept(fd, (sockaddr*)&client_addr, &client_len);
	set_nonblocking(client_fd);
	ClientConnection *client_conn = new ClientConnection();
	client_conn->fd = client_fd;
	if (client_fd < 0) return; // TODO handle error?

	loop.add(client_conn);
}

void	ClientConnection::enqueue_response(EpollLoop &loop, const std::string &response) {
	_write_buf = response;
	_write_offset = 0;
	// tell epoll- wake me up when this fd is writable
	loop.mod(this, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);
}

void ClientConnection::handle(EpollLoop &loop, uint32_t events) {
	if (events & EPOLLIN) {
		char buffer[24];
		int bytes = read(fd, buffer, sizeof(buffer) - 1);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			loop.del(this);
			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
		}
		if (bytes == 0) {
			loop.del(this);
		} else {
			_parser.feed(buffer, bytes);
			std::cout << "parsing..." << _parser._i << std::endl;

			if (_parser.complete()) {
				buffer[bytes] = '\0';
				std::cout << "parser done being fed" << std::endl;
				// hand off to request handler
				// eventually enqueue_response()
				std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
				enqueue_response(loop, response);
			}
			// buffer[bytes] = '\0';
			// std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
			// enqueue_response(loop, response);
		}
	}
	if (events & EPOLLOUT) {
		const char *data = _write_buf.c_str() + _write_offset;
		size_t remaining = _write_buf.size() - _write_offset;

		int sent = write(fd, data, remaining);

		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			loop.del(this);
		} else {
			_write_offset += sent;
			if (_write_offset == _write_buf.size()) {
				_write_buf.clear();
				_write_offset = 0;
				loop.mod(this, EPOLLIN | EPOLLERR | EPOLLHUP);
			}
		}
	}
}
