#pragma once
//#include "ServerBlock.hpp"
#include "Http.hpp"
#include <stdint.h>

class EpollLoop;

class Connection {
		// TODO constructors, destructors etc
	public:
		virtual void handle(EpollLoop &loop, uint32_t events) = 0;
		virtual ~Connection() {}
		int	fd;
	//:w
	//ServerBlock *config;
		Http *http;
	protected:
};
