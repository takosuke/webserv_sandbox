#pragma once
#include <map>
#include <set>
#include <sys/epoll.h>

class Connection;

#define MAX_EVENTS 64

class EpollLoop
{
	public:
		static EpollLoop &	get_instance();

		void add(Connection *conn);
		void mod(Connection *conn, uint32_t events);
		void del(Connection *conn);
		void clear();
		void run();

	private:
		EpollLoop();
		EpollLoop(const EpollLoop &);
		~EpollLoop();

		EpollLoop & operator=(const EpollLoop &);

		void	delete_conn(Connection * conn);

		int	_epoll_fd;
		std::map<int, Connection*> _connections;
		epoll_event	_events[MAX_EVENTS];
		std::set<Connection *>		_deletion_queue;
};
