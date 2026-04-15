#pragma once
#include "config.hpp"

void	set_nonblocking(int fd);
int		make_server_socket(const config::listen &l);
