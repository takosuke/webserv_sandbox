#include <asm-generic/socket.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

int main() {
	// 1. Create socket
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		std::cerr << "socket() failed" << std::endl;
		return 1;
	}
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);

	if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
	{
		std::cerr << "bind() failed" << std::endl;
		return 1;
	}

	if (listen(server_fd, 10) < 0) 
	{
		std::cerr << "Listen() failed" << std::endl;
		return 1;
	}

	std::cout << "Listnening on port 8080..." << std::endl;
	while (true)
	{
		sockaddr_in client_addr;
		memset(&client_addr, 0, sizeof(client_addr));
		socklen_t client_len = sizeof(client_addr);

		int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
		if (client_fd < 0)
		{
			std::cerr << "accept() failed" << std::endl;
			continue;
		}
		std::cout << "Connection accepted! fd=" << client_fd << std::endl;
		char buf[4096];
		int b = read(client_fd, buf, sizeof(buf) - 1);
		if (b > 0)
		{
			buf[b] = '\0';
			std::cout << "request: " << buf << " ~~~~~~~~~~~~" << std::endl;
		}
	}
	close(server_fd);
	return 0;

}
