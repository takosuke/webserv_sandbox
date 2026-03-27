#include <asm-generic/socket.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

#define MAX_EVENTS 64

int make_server_socket()
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);

	bind(fd, (sockaddr*)&addr, sizeof(addr));
	listen(fd, 10);
	return fd;
}

int main()
{
	int server_fd = make_server_socket();
	std::cout << "Listening on port 8080..." << std::endl;

	int epoll_fd = epoll_create1(0);
	if (epoll_fd < 0)
	{
		std::cerr << "epoll_create1() failed" << std::endl;
		return 1;
	}

	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = server_fd;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

	epoll_event events[MAX_EVENTS];

	while (true)
	{
		// Block until events arrive — returns ONLY ready fds
		int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (ready < 0)
		{
			std::cerr << "epoll_wait() failed" << std::endl;
			break;
		}
		// Iterate only the ready events, not all fds
		for (int i = 0; i < ready; i++)
		{
			int fd = events[i].data.fd; 
			if (fd == server_fd)
			{
				// new connection
				sockaddr_in client_addr;
				socklen_t client_len = sizeof(client_addr);
				int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
				if (client_fd < 0) continue;

				std::cout << "New connection fd=" << client_fd << std::endl;
				epoll_event client_ev;
				memset(&client_ev, 0, sizeof(client_ev));
				client_ev.events = EPOLLIN;
				client_ev.data.fd = client_fd;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
			}
			else
			{
				// Existing client brings data
				char buffer[4096];
				int bytes = read(fd, buffer, sizeof(buffer));
				if (bytes <= 0)
				{
					std::cout << "Client fd=" << fd << "disconnected" << std::endl;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL); // unregister
					close(fd);
				}
				else
				{
					buffer[bytes] = '\0';
					std::cout << "fd=" << fd << ": " << buffer;
					write(fd, buffer, bytes); // echo back;
				}
			}
		}
	}
	close(epoll_fd);
	close(server_fd);
	return 0;
}
