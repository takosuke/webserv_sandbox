#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include "Connection.hpp"
#include "ServerBlock.hpp"

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

void enqueue_response(int epoll_fd, Connection *conn, const std::string &response) {
	conn->write_buf = response;
	conn->write_offset = 0;

	// tell epoll- wake me up when this fd is writable
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLOUT;
	ev.data.ptr = conn;
	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

int main() {
	// Should be getting these from parsing the config file
	ServerBlock block_1(8080, "localhost", "/var/www/");
	ServerBlock block_2(8081, "localhost", "/var/www/");
	// maybe they don't need to be pointers
	std::vector<ServerBlock*> server_blocks;
	server_blocks.push_back(&block_1);
	server_blocks.push_back(&block_2);

    // Create the epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1() failed\n";
        return 1;
    }

	// map of active connections, will be useful later
	std::map<int, Connection*> active_conns;

	for (std::vector<ServerBlock*>::iterator it = server_blocks.begin();
		it != server_blocks.end(); ++it)
	{
		int server_fd = make_server_socket((*it)->port);
		Connection *server_conn = new Connection();
		server_conn->fd = server_fd;
		server_conn->type = Connection::SERVER;
		std::cout << "Listening on port " << (*it)->port << "...\n";

		// Register the listening socket
		epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.events  = EPOLLIN;
		ev.data.ptr = server_conn;
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
		active_conns[server_fd] = server_conn;
	}

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
			Connection *conn = (Connection*)events[i].data.ptr;

            if (conn->type == Connection::SERVER) {
                // New connection
                sockaddr_in client_addr;
                socklen_t   client_len = sizeof(client_addr);
                int client_fd = accept(conn->fd, (sockaddr*)&client_addr, &client_len);
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
				// Error or hangup
				if (events[i].events & (EPOLLERR | EPOLLHUP))
				{
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
					active_conns.erase(conn->fd);
					close(conn->fd);
					delete conn;
				}
                // Existing client has data
				if (events[i].events & EPOLLIN)
				{
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
	//                    std::cout << "fd=" << conn->fd << ": " << buffer;
						//mock response
						std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nidiot";
						enqueue_response(epoll_fd, conn, response);
					}
				}

				if (events[i].events & EPOLLOUT)
				{
					const char *data = conn->write_buf.c_str() + conn->write_offset;
					size_t	remaining = conn->write_buf.size() - conn->write_offset;

					int sent = write(conn->fd, data, remaining);

					if (sent < 0)
					{
						//error, close connection
						epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
						active_conns.erase(conn->fd);
						close(conn->fd);
						delete conn;
					} else {
						conn->write_offset += sent;
						
						if (conn->write_offset == conn->write_buf.size())
						{
							//fully sent, stop watching for EPOLLOUT
							conn->write_buf.clear();
							conn->write_offset = 0;

							epoll_event ev;
							memset(&ev, 0, sizeof(ev));
							ev.events = EPOLLIN;
							ev.data.ptr = conn;
							epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
						}
						// if not fully sent, leave EPOLLOUT registered
						// next wakeup will continue from write_offset
					}
				}
            }
			// send buffered data
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
