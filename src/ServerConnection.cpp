#include <exception>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "ServerConnection.hpp"
#include "ClientConnection.hpp"
#include "EpollLoop.hpp"
#include "utils.hpp"

void	ServerConnection::handle(uint32_t events) {

	if (events & (EPOLLERR | EPOLLHUP)) {
		EpollLoop::get_instance().del(this);
		return ;
	}
	sockaddr_in client_addr;
	socklen_t	client_len = sizeof(client_addr);
	int client_fd = accept(fd, (sockaddr*)&client_addr, &client_len);
	if (client_fd < 0) {
		if (errno == EMFILE || errno == ENFILE) {
			/* Out of descriptors. The listen socket stays readable under
			* level-triggered epoll, so returning here spins at ~100% CPU.
			* Surrender the reserve, accept-and-close so the backlog entry is
			* drained and the client gets a clean close instead of a hang,
			* then take the reserve back. */
			EpollLoop::get_instance().release_reserve_fd();
			int tmp = accept(fd, NULL, NULL);
			if (tmp >= 0)
				close(tmp);
			EpollLoop::get_instance().reclaim_reserve_fd();
		}
		return ;
	}
	try {
		set_nonblocking(client_fd);
		set_cloexec(client_fd);
	} catch (std::exception &e) {
		close(client_fd);
		throw;
	}
	Connection *client_conn = new ClientConnection(client_fd, http, addr);
	EpollLoop::get_instance().add(client_conn);
}

ServerConnection::~ServerConnection() {
	if (fd != -1)
		close(fd);
}
