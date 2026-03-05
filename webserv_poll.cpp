#include <asm-generic/socket.h>
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

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
	std::cout << "listening on oprt 8080..." << std::endl;

	std::vector<pollfd> fds;
	pollfd server_pfd;
	server_pfd.fd = server_fd;
	server_pfd.events = POLLIN;
	server_pfd.revents = 0;
	fds.push_back(server_pfd);

	while (true)
	{
		int ready = poll(&fds[0], fds.size(), -1);
		if (ready < 0)
		{
			std::cerr << "poll() failed" << std::endl;
			break;
		}

		for (size_t i = 0; i < fds.size(); i++)
		{
			if (!(fds[i].revents & POLLIN))
				continue;

			if (fds[i].fd == server_fd)
			{
				sockaddr_in client_addr;
				socklen_t client_len = sizeof(client_addr);
				int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
				if (client_fd < 0) continue;

				std::cout << "New connection fd=" << client_fd << std::endl;

				pollfd client_pfd;
				client_pfd.fd = client_fd;
				client_pfd.events = POLLIN;
				client_pfd.revents = 0;
				fds.push_back(client_pfd);

			} else {
				char buf[4096];
				int b = read(fds[i].fd, buf, sizeof(buf) - 1);
				if (b <= 0)
				{
					std::cout << "Client fd=" << fds[i].fd << " disconnected" << std::endl;
					close(fds[i].fd);
					fds.erase(fds.begin() + i);
					i--;
				} else {
					buf[b] = '\0';
					std::cout << "fd=" << fds[i].fd << ": " << buf;
					write(fds[i].fd, buf, b);
				}
			}
		}
	}
	close(server_fd);
	return 0;
}
