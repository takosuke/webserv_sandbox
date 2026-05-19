#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

#include "ClientConnection.hpp"
#include "FileConnection.hpp"
#include "EpollLoop.hpp"

ClientConnection::~ClientConnection() {
	close(_file_fd);
	delete (_file_connection);
}

void	ClientConnection::enqueue_response() {
	// tell epoll- wake me up when this fd is writable
	EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
}

void	ClientConnection::handle(uint32_t events) {
	if (events & EPOLLIN) {
		// small buffer to test for multiple reads
		// TODO make it a big number!
		char buffer[24];
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
			if (_parser.getRequest().error)
				std::cout << "parser error" << std::endl; // route to error page
			if (_parser.complete()) {
				switch (_parser.getRequest().method) {
					case GET: handle_get(); break ;
					case POST: break ;
					case DELETE: break ;
					case UNKNOWN: break ;
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
		
		if (_response.write_count(fd, 1024) <= 0) {
			EpollLoop::get_instance().del(this);
		}
	}
}

void	ClientConnection::handle_get() {
	const Request &	req = _parser.getRequest();
	_file_connection = new ReadFileConnection(this, EPOLLOUT | EPOLLERR | EPOLLHUP, _response.entity);

	std::string	path = req.location->get_root() + req.path;

	_file_fd = open(path.c_str(), O_RDONLY);
	if (_file_fd < 0)
		// TODO handle 404 error
		return ;
	_file_connection->set_fd(_file_fd);
	_file_connection->set_buffer_size(BYTES_PER_READ_CYCLE);
	EpollLoop::get_instance().mod(this, 0);
	FileLoop::get_instance().add(_file_connection);
}

/** @brief Constructs the response based on the `code` recieved
 *
 *  Called from a sub connection or itself, whenever a response is ready to
 *  be created.
 */ 
void	ClientConnection::construct_response(int code) {
	_response.construct_status_line(HTTP_VERSION_STR, code);
	_response.add_content_length();
	std::cout << _response << std::endl;
}
