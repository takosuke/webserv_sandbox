#pragma once

#include "Config.hpp"

void	set_nonblocking(int fd);
int		make_server_socket(const config::listen &l);
