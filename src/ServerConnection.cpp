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
	if (client_fd < 0) return;
	set_nonblocking(client_fd);
	set_cloexec(client_fd);
	Connection *client_conn = new ClientConnection(client_fd, http, addr);

	EpollLoop::get_instance().add(client_conn);
}

ServerConnection::~ServerConnection() {
	if (fd != -1)
		close(fd);
}
