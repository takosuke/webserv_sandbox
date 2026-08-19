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

void	set_cloexec(int fd) { 
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		throw std::runtime_error(std::string("fcntl F_SETFD failed: ") + strerror(errno));
}

int		make_server_socket(const config::listen &l) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

bool	equals_icase(const std::string &a, const std::string &b) {
	if (a.size() != b.size())
		return false;
	for (std::string::size_type i = 0; i < a.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(a[i]))
				!= std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return true;
}
