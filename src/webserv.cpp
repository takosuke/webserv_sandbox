#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "ServerConnection.hpp"
#include "ServerBlock.hpp"
#include "EpollLoop.hpp"
#include "utils.hpp"

#define MAX_EVENTS 64

int main() {
	// Should be getting these from parsing the config file
	ServerBlock block_1(8080, "localhost", "/var/www/");
	ServerBlock block_2(8081, "localhost", "/var/www/");
	// maybe they don't need to be pointers
	std::vector<ServerBlock*> server_blocks;
	server_blocks.push_back(&block_1);
	server_blocks.push_back(&block_2);

    // Create the epoll instance
	try {
		EpollLoop loop;

		for (std::vector<ServerBlock*>::iterator it = server_blocks.begin();
			it != server_blocks.end(); ++it)
		{
			int server_fd = make_server_socket((*it)->port);
			ServerConnection *server_conn = new ServerConnection();
			server_conn->fd = server_fd;
			std::cout << "Listening on port " << (*it)->port << "...\n";

			// Register the listening socket
			loop.add(server_conn);
		}

		loop.run();
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}
	//
	// TODO Cleanup
    return 0;
}
