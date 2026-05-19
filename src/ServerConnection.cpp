#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Config.hpp"
#include "ServerConnection.hpp"
#include "ClientConnection.hpp"
#include "EpollLoop.hpp"
#include "utils.hpp"

void	ServerConnection::handle(uint32_t events) {

	(void)events;
	sockaddr_in client_addr;
	socklen_t	client_len = sizeof(client_addr);
	int client_fd = accept(fd, (sockaddr*)&client_addr, &client_len);
	if (client_fd < 0) return; // TODO handle error?
	set_nonblocking(client_fd);
	ClientConnection *client_conn = new ClientConnection(client_fd, http, addr);

	EpollLoop::get_instance().add(client_conn);
}
