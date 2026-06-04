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
#include "EpollLoop.hpp"

ClientConnection::~ClientConnection() {

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
				switch (_parser.getRequest().method) {
					case GET: handle_get(); break ;
					case POST: break ;
					case DELETE: break ;
					case UNKNOWN: break ;
				}
				EpollLoop::get_instance().mod(this, EPOLLOUT | EPOLLERR | EPOLLHUP);
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
