#include <iostream>
#include "ServerConnection.hpp"
//#include "ServerBlock.hpp"
#include "EpollLoop.hpp"
#include "utils.hpp"

#include "Http.hpp"
#include "Grouper.hpp"
#define MAX_EVENTS 64

int main(int ac, char *av[]) {
	// Should be getting these from parsing the config file
//	ServerBlock block_1(8080, "localhost", "/var/www/");
//	ServerBlock block_2(8081, "localhost", "/var/www/");
	// maybe they don't need to be pointers
	//std::vector<ServerBlock*> server_blocks;
	//server_blocks.push_back(&block_1);
	//server_blocks.push_back(&block_2);
	// Arns config thing
	const char *config_path = (ac > 1) ? av[1] : "webserv.conf";
	Grouper grouper(config_path);
	if (!grouper.group()) {
		std::cerr << "Failed to parse config" << std::endl;
		return 1;
	}
	Http http(grouper.main.body_directives[0]);


    // Create the epoll instance
	try {
		EpollLoop loop;

		const std::map<struct sockaddr_in, Port> &ports = http.get_ports();

//		for (std::vector<ServerBlock*>::iterator it = server_blocks.begin();
//			it != server_blocks.end(); ++it)
		for (std::map<struct sockaddr_in, Port>::const_iterator it = ports.begin();
				it != ports.end(); ++it)
		{
			const config::listen &l = it->second.get_listen();

			ServerConnection *server_conn = new ServerConnection();
			server_conn->fd   = make_server_socket(l);
			server_conn->http = &http;
			server_conn->addr = l.get_sockaddr();
			std::cout << "Listening on port " << l.port << "...\n";

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
