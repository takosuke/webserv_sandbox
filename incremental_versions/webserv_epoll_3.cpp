#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include "Connection.hpp"

#define MAX_EVENTS 64

int make_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1)
	{
		std::cerr << "Socket bind failed" << std::endl;
		return -1;
	}
    listen(fd, 10);
    return fd;
}

int main() {
	ServerBlock default_server = {8080, "localhost", "/var/www/site", {}};
    int server_fd = make_server_socket(default_server.port);
	Connection *server_conn = new Connection();
	server_conn->fd = server_fd;
	server_conn->type = Connection::SERVER;
    std::cout << "Listening on port 8080...\n";

    // Create the epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1() failed\n";
        return 1;
    }

	// map of active connections, will be useful later
	std::map<int, Connection*> active_conns;

    // Register the listening socket
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.ptr = server_conn;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
	active_conns[server_fd] = server_conn;

    epoll_event events[MAX_EVENTS];

    while (true) {
        // Block until events arrive — returns ONLY ready fds
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (ready < 0) {
            std::cerr << "epoll_wait() failed\n";
            break;
        }

        // Iterate only the ready events, not all fds
        for (int i = 0; i < ready; i++) {
            int fd = events[i].data.fd;
			Connection *conn = (Connection*)events[i].data.ptr;


            if (conn->type == Connection::SERVER) {
                // New connection
                sockaddr_in client_addr;
                socklen_t   client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
				Connection *client_conn = new Connection();
				client_conn->fd = client_fd;
				client_conn->type = Connection::CLIENT;
                if (client_fd < 0) continue;

                std::cout << "New connection fd=" << client_fd << "\n";

                // Register new client with epoll
                epoll_event client_ev;
                memset(&client_ev, 0, sizeof(client_ev));
                client_ev.events  = EPOLLIN;
                client_ev.data.ptr = client_conn;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
				active_conns[client_fd] = client_conn;

            } else {
                // Existing client has data
                char buffer[4096];
                int  bytes = read(conn->fd, buffer, sizeof(buffer) - 1);

                if (bytes <= 0) {
                    std::cout << "Client fd=" << conn->fd << " disconnected\n";
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL); // unregister
					active_conns.erase(conn->fd);
                    close(conn->fd);
					delete conn;
                } else {
                    buffer[bytes] = '\0';
                    std::cout << "fd=" << conn->fd << ": " << buffer;
                    write(conn->fd, buffer, bytes); // echo back
                }
            }
        }
    }

	// Cleanup
	for (std::map<int, Connection*>::iterator it = active_conns.begin();
			it != active_conns.end(); ++it) 
	{
		close(it->second->fd);
		delete it->second;
	}
    close(epoll_fd);
    return 0;
}
