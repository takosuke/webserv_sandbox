#include "utils.hpp"
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>

void	set_nonblocking(int fd) {
	int	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		throw std::runtime_error(std::string("fcntl F_GETFL failed: ") + strerror(errno));
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		throw std::runtime_error(std::string("fcntl F_SETFL failed: ") + strerror(errno));
}

int	make_server_socket(int port) {
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
	set_nonblocking(fd);
    listen(fd, 10);
    return fd;
}
