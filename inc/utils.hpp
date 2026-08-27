#pragma once

#include "Config.hpp"

void	set_nonblocking(int fd);
int		make_server_socket(const config::listen &l);
bool	equals_icase(const std::string &a, const std::string &b); 
std::string	to_lower(const std::string &s);
void	set_cloexec(int fd);
