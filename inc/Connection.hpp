#pragma once
//#include "ServerBlock.hpp"
#include "Config.hpp"

#include <stdint.h>

class EpollLoop;

class Connection {
		// TODO constructors, destructors etc
	public:
		Connection() : fd(0), http(NULL) {}
		Connection(int fildes, Http * config)
			: fd(fildes), http(config) { };
		virtual void handle(uint32_t events) = 0;
		virtual ~Connection() {}
		int	fd;
	//ServerBlock *config;
		Http *	http;
	protected:
};
