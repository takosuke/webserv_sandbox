#pragma once
#include "Config.hpp"

#include <stdint.h>

class EpollLoop;

class Connection {
	public:
		Connection() : fd(0), http(NULL), _last_update(time(NULL)), _timeout(-1) {}
		Connection(int fildes, Http * config)
			: fd(fildes), http(config), _last_update(time(NULL)), _timeout(-1) { };
		virtual void handle(uint32_t events) = 0;
		virtual ~Connection() {}
		int	fd;
		Http *	http;

		time_t				_last_update;
		time_t				_timeout;
	protected:
};
