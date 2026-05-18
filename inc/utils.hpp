#pragma once

#include "Config.hpp"

void	set_nonblocking(int fd);
int		make_server_socket(const config::listen &l);
bool	is_digit_string(const std::string &s);
