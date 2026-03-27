#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include "Connection.hpp"
#include "ServerBlock.hpp"
#include "EpollLoop.hpp"

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
	// Should be getting these from parsing the config file
	ServerBlock block_1(8080, "localhost", "/var/www/");
	ServerBlock block_2(8081, "localhost", "/var/www/");
	// maybe they don't need to be pointers
	std::vector<ServerBlock*> server_blocks;
	server_blocks.push_back(&block_1);
	server_blocks.push_back(&block_2);

    // Create the epoll instance
	EpollLoop loop;


	for (std::vector<ServerBlock*>::iterator it = server_blocks.begin();
		it != server_blocks.end(); ++it)
	{
		int server_fd = make_server_socket((*it)->port);
		Connection *server_conn = new Connection();
		server_conn->fd = server_fd;
		server_conn->type = Connection::SERVER;
		std::cout << "Listening on port " << (*it)->port << "...\n";

		// Register the listening socket
		loop.add(server_conn);
	}

	loop.run();
	//
	// Cleanup
    return 0;
}
