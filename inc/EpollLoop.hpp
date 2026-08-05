#pragma once
#include <map>
#include <set>
#include <ctime>
#include <sys/types.h>
#include <sys/epoll.h>

class Connection;

#define MAX_EVENTS 64
#define CGI_KILL_GRACE 2

class EpollLoop
{
	public:
		static EpollLoop &	get_instance();

		void	add(Connection *conn);
		void	mod(Connection *conn, uint32_t events);
		void	del(Connection *conn);
		void	rearm(Connection *conn, uint32_t events, int new_fd);
		void	clear();
		void	run();
		void	track_child(pid_t pid, time_t timeout);
		void	kill_child(pid_t pid);

	private:
		struct Child {
			time_t	deadline;
			bool	signalled;
			Child() : deadline(0), signalled(false) { }
		};
		void	reap_children();

		EpollLoop();
		EpollLoop(const EpollLoop &);
		~EpollLoop();

		EpollLoop & operator=(const EpollLoop &);

		void	delete_conn(Connection * conn);


		int		_epoll_fd;
		std::map<int, Connection*> _connections;
		epoll_event	_events[MAX_EVENTS];
		std::set<Connection *>		_deletion_queue;
		std::map<pid_t, Child>		_children;
};
