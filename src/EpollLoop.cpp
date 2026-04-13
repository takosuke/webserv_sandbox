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
			conn->handle(*this, _events[i].events);
		}
	}
}
