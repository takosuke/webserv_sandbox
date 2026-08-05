#include <cerrno>
#include <stdexcept>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "EpollLoop.hpp"
#include "Connection.hpp"
#include "utils.hpp"
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>

#include "Logger.hpp"
#include "ClientConnection.hpp"

static volatile sig_atomic_t	sig_int	= 0;

void	int_handler(int) {
	sig_int = 1;
}

EpollLoop & EpollLoop::get_instance() {
	static EpollLoop instance;
	return instance;
}


EpollLoop::EpollLoop() {
	_epoll_fd = epoll_create1(0);
    if (_epoll_fd < 0) {
		throw std::runtime_error(std::string("epoll_create1() failed: ") + strerror(errno));
    }
}

EpollLoop::~EpollLoop() {
	for (std::map<pid_t, Child>::iterator it = _children.begin(); it != _children.end(); ++it) {
		kill(it->first, SIGKILL);
		while (waitpid(it->first, NULL, 0) < 0 && errno == EINTR)
			;
	}
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
	// TODO error check EPOLL_CTL_ADD
	epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
	_connections[conn->fd] = conn;
}

void	EpollLoop::del(Connection * conn) {
	_deletion_queue.insert(conn);
}

void	EpollLoop::mod(Connection *conn, uint32_t events) {
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.ptr = conn;
	// TODO error check EPOLL_CTL_MOD
	epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

void	EpollLoop::rearm(Connection *conn, uint32_t events, int new_fd) {
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
	_connections.erase(conn->fd);
	conn->fd = new_fd;
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.ptr = conn;
	epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
	_connections[new_fd] = conn;
}

void	EpollLoop::clear() {
	for (std::set<Connection *>::iterator it = _deletion_queue.begin();
		it != _deletion_queue.end(); it++) {
		delete_conn(*it);
	}
	_deletion_queue.clear();
}

void	EpollLoop::run() {
	sig_int = 0;
	signal(SIGINT, int_handler);
	while (!sig_int) {
		int ready = epoll_wait(_epoll_fd, _events, MAX_EVENTS, 5);
		int err = errno;
		if (ready < 0) {
			if (err != EINTR && sig_int == false) {
				std::cerr << "epoll_wait() failed: " << strerror(err) << std::endl;
				break ;
			}
			ready = 0;
		}
		// TODO events.data after i can become dangling pointer if kernel
		// returns events after i in the same epoll_wait batch (possible when
		// EPOLLIN and EPOLLHUP arrive together) - should skip remaining
		// processing for this fd after del(conn) or check _connections.count
		// before dereferencing
		for (int i = 0; i < ready; i++) {
			Connection *conn = (Connection*)_events[i].data.ptr;
			try {
				conn->handle(_events[i].events);
			} catch (const std::exception &e) {
				std::cerr << "connection " << conn->fd << " threw: " << e.what() << std::endl;
			} catch (...) {
				std::cerr << "connection " << conn->fd << " threw a custom exception" << std::endl;
			}
		}
		time_t	cur_time = time(NULL);
		for (std::map<int, Connection *>::const_iterator it = _connections.begin();
			it != _connections.end(); it++) {
		ClientConnection  * clicon = dynamic_cast<ClientConnection *>(it->second);
		if (clicon != NULL && cur_time - clicon->_last_update > clicon->_timeout) {
			try {
				clicon->handle_timeout();
			} catch (const std::exception &e) {
				std::cerr << "connection " << clicon->fd << " threw: " << e.what() << std::endl;
			} catch (...) {
				std::cerr << "connection " << clicon->fd << " threw a custom exception" << std::endl;
			}
      }
		}
		clear();
		reap_children();
	}
	if (sig_int != 0)
		LOG_DEBUG("INFO") << "SIGINT (" << sig_int << ") received. Shutting down" << std::endl;
}

void	EpollLoop::delete_conn(Connection *conn) {
	std::cout << "Client fd=" << conn->fd << " disconnected\n";
	// TODO error check EPOLL_CTL_DEL
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
	_connections.erase(conn->fd);
	close(conn->fd);
	delete conn; // do we delete it here or wait for destructor
}

void	EpollLoop::track_child(pid_t pid, time_t timeout) {
	if (pid <= 0)
		return ;
	Child c;
	c.deadline = time(NULL) + timeout;
	_children[pid] = c;
}

// Mark child as not wanted, next sweep signals and reaps it
void	EpollLoop::kill_child(pid_t pid) {
	std::map<pid_t, Child>::iterator it = _children.find(pid);
	if (it == _children.end())
		return ;
	it->second.deadline = 0;
}

void	EpollLoop::reap_children() {
	time_t now = time(NULL);

	std::map<pid_t, Child>::iterator it = _children.begin();
	while (it != _children.end()) {
		pid_t	ret = waitpid(it->first, NULL, WNOHANG);
		// >0 means child reaped. ECHILD means it's gone. 
		// Either way we stop tracking it
		if (ret > 0 || (ret < 0 && errno == ECHILD)) {
			_children.erase(it++);
			continue ;
		}
		if (now >= it->second.deadline) {
			if (it->second.signalled == false) {
				LOG_WARN("cgi") << "CGI pid " << it->first << " overran, sending SIGTERM" << std::endl;
				kill(it->first, SIGTERM);
				it->second.signalled = true;
			} else {
				LOG_WARN("cgi") << "CGI pid " << it->first << " ignored SIGTERM, sending SIGKILL" << std::endl;
				kill(it->first, SIGKILL);
			}
			it->second.deadline = now + CGI_KILL_GRACE;
		}
		++it;
	}
}
