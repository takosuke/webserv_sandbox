#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
#include <stdexcept>
#include "ClientConnection.hpp"
#include "EpollLoop.hpp"

void	ClientConnection::enqueue_response(EpollLoop &loop) {
	// tell epoll- wake me up when this fd is writable
	loop.mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
}

void	ClientConnection::handle(EpollLoop &loop, uint32_t events) {
	if (events & EPOLLIN) {
		// small buffer to test for multiple reads
		// TODO make it a big number!
		char buffer[24];
		int bytes = read(fd, buffer, sizeof(buffer) - 1);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			loop.del(this);
			// TODO throw will kill the server - should wrap conn->handle
			// in try-catch
			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
		}
		if (bytes == 0) {
			loop.del(this);
		} else {
			_parser.feed(buffer, bytes);

			if (_parser.getRequest().error)
				std::cout << "parser error" << std::endl; // route to error page
			if (_parser.complete()) {
				// // Response res(_parser.request);
				// // make route
				// // hand off to request handler
				// // eventually enqueue_response()
				// buffer[bytes] = '\0';
				// std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
				_response.construct_status_line(HTTP_VERSION_STR, 200);
				_response.add_content_length();
				_response.entity = std::string("Hello, World");
				std::cout << _response << std::endl;
				
				enqueue_response(loop);
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
		// 	loop.del(this);
		// } else {
		// 	_write_offset += sent;
		// 	if (_write_offset == _write_buf.size()) {
		// 		_write_buf.clear();
		// 		_write_offset = 0;
		// 		loop.mod(this, EPOLLIN | EPOLLERR | EPOLLHUP);
		// 	}
		// }
		
		if (_response.write_count(fd, 1024) <= 0) {
			loop.del(this);
		}
	}
}
