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

int	make_server_socket(const config::listen &l) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    const sockaddr_in &addr = l.get_sockaddr();
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1)
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));

    if (::listen(fd, l.backlog) == -1)
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));

    return fd;
}
