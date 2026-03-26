#pragma once
#include <map>
#include <sys/epoll.h>

struct Connection;

#define MAX_EVENTS 64

class EpollLoop
{
	public:
		EpollLoop();
		~EpollLoop();
		void add(Connection *conn);
		void mod(Connection *conn, uint32_t events);
		void del(Connection *conn);
		void run();

	private:
		int	_epoll_fd;
		std::map<int, Connection*> _connections;
		epoll_event	_events[MAX_EVENTS];
		
		void _handle_server(Connection *conn);
		void _handle_client(Connection *conn, uint32_t events);
};
