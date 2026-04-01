#include <cerrno>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include "EpollLoop.hpp"
#include "Connection.hpp"
#include "utils.hpp"
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

EpollLoop::EpollLoop() {
	_epoll_fd = epoll_create1(0);
    if (_epoll_fd < 0) {
		throw std::runtime_error(std::string("epoll_create1() failed: ") + strerror(errno));
    }
}

EpollLoop::~EpollLoop() {
	for (std::map<int, Connection*>::iterator it = _connections.begin();
			it != _connections.end(); ++it) 
	{
		close(it->second->fd);
		delete it->second;
	}
    close(_epoll_fd);
}

void	EpollLoop::add(Connection *conn) {
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	ev.data.ptr = conn;
	epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
	_connections[conn->fd] = conn;
}

void	EpollLoop::mod(Connection *conn, uint32_t events) {
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.ptr = conn;
	epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

void	EpollLoop::del(Connection *conn) {
	std::cout << "Client fd=" << conn->fd << " disconnected\n";
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
	_connections.erase(conn->fd);
	close(conn->fd);
	delete conn; // do we delete it here or wait for destructor
}

void	EpollLoop::_handle_server(Connection *conn) {

	sockaddr_in client_addr;
	socklen_t	client_len = sizeof(client_addr);
	int client_fd = accept(conn->fd, (sockaddr*)&client_addr, &client_len);
	set_nonblocking(client_fd);
	ClientConnection *client_conn = new ClientConnection();
	client_conn->fd = client_fd;
	client_conn->type = Connection::CLIENT;
	if (client_fd < 0) return; // TODO handle error?

	std::cout << "New connection fd=" << client_fd << std::endl;
	add(client_conn);
}

void	EpollLoop::_handle_client(Connection *conn, uint32_t events) {
	if (events & EPOLLIN) {
		char buffer[4096];
		int bytes = read(conn->fd, buffer, sizeof(buffer) - 1);
		if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			throw std::runtime_error(std::string("Read from client error: ") + strerror(errno));
		}
		if (bytes == 0) {
			del(conn);
		} else {
			buffer[bytes] = '\0';
			std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
			conn->enqueue_response(*this, response);
		}
	}
	if (events & EPOLLOUT) {
		const char *data = conn->write_buf.c_str() + conn->write_offset;
		size_t remaining = conn->write_buf.size() - conn->write_offset;

		int sent = write(conn->fd, data, remaining);

		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			del(conn);
		} else {
			conn->write_offset += sent;
			if (conn->write_offset == conn->write_buf.size()) {
				conn->write_buf.clear();
				conn->write_offset = 0;
				mod(conn, EPOLLIN | EPOLLERR | EPOLLHUP);
			}
		}
	}

}

void	EpollLoop::run() {
	while (true) {
		int ready = epoll_wait(_epoll_fd, _events, MAX_EVENTS, -1);
		if (ready < 0) {
			std::cerr << "epoll_wait() failed" << std::endl; // TODO throw exception
			// here
			break;
		}
		for (int i = 0; i < ready; i++) {
			Connection *conn = (Connection*)_events[i].data.ptr;
			if (_events[i].events & (EPOLLERR | EPOLLHUP)) {
				del(conn);
				continue;
			}
			if (conn->type == Connection::SERVER) {
				_handle_server(conn);
			}
			if (conn->type == Connection::CLIENT) {
				_handle_client(conn, _events[i].events);
			}
		}
	}
}
