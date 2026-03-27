#include <asm-generic/socket.h>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

#define MAX_EVENTS 64

struct Connection {
	int	fd;
	enum Type { SERVER, CLIENT } type;
	// or more: which server block it belongs to, request state, etc.
	std::string write_buf;
	size_t write_offset;
};

int make_server_socket()
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);

	bind(fd, (sockaddr*)&addr, sizeof(addr));
	listen(fd, 10);
	return fd;
}

int main()
{
	int server_fd = make_server_socket();
	Connection *server_conn = new Connection();
	server_conn->fd = server_fd;
	server_conn->type = Connection::SERVER;
	std::cout << "Listening on port 8080..." << std::endl;

	int epoll_fd = epoll_create1(0);
	if (epoll_fd < 0)
	{
		std::cerr << "epoll_create1() failed" << std::endl;
		return 1;
	}

	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	// We keep a map when we have a connection object so we can iterate
	// over the interest list and curb inactive ones etc (we cant access
	// interest list directly)
	std::map<int, Connection*> active_connections;
	ev.events = EPOLLIN;
	ev.data.ptr = server_conn;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
	active_connections[server_fd] = server_conn;

	epoll_event events[MAX_EVENTS];

	while (true)
	{
		// Block until events arrive — returns ONLY ready fds
		int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (ready < 0)
		{
			std::cerr << "epoll_wait() failed" << std::endl;
			break;
		}
		// Iterate only the ready events, not all fds
		for (int i = 0; i < ready; i++)
		{
			Connection *conn = (Connection*)events[i].data.ptr;
			if (conn->type == Connection::SERVER)
			{
				// new connection
				sockaddr_in client_addr;
				socklen_t client_len = sizeof(client_addr);
				int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
				if (client_fd < 0) continue;

				std::cout << "New connection fd=" << client_fd << std::endl;
				epoll_event client_ev;
				memset(&client_ev, 0, sizeof(client_ev));
				client_ev.events = EPOLLIN;
				Connection *conn = new Connection();
				conn->fd = client_fd;
				conn->type = Connection::CLIENT;
				client_ev.data.ptr = conn;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
				active_connections[client_fd] = conn;
			}
			else
			{
				// Existing client brings data
				char buffer[4096];
				int bytes = read(conn->fd, buffer, sizeof(buffer));
				if (bytes <= 0)
				{
					std::cout << "Client fd=" << conn->fd << "disconnected" << std::endl;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL); // unregister
					close(conn->fd);
					active_connections.erase(conn->fd);
				}
				else
				{
					buffer[bytes] = '\0';
					std::cout << "fd=" << conn->fd << ": " << buffer;
					write(conn->fd, buffer, bytes); // echo back;
					std::cout << "Active connections: " ;
					for (std::map<int, Connection*>::iterator it = active_connections.begin(); it != active_connections.end(); ++it)
					{
						std::cout << it->first << " " << it->second->type << "||";
					}
					std:: cout << std::endl;

				}
			}
		}
	}
	close(epoll_fd);
	close(server_fd);
	return 0;
}
