#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

int	main(int ac, char *av[]) {

	if (ac != 3) {
		fprintf(stderr, "Usage: %s host port\n", av[0]);
		exit(1);
	}

	struct addrinfo	hints;
	struct addrinfo	*result, *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		/* Allow IPv4 and IPv6 */
	hints.ai_socktype = SOCK_STREAM;	/* TCP socket */
	hints.ai_flags = AI_PASSIVE;		/* For wildcard IP addresses */
	hints.ai_protocol = 0;				/* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	int s = getaddrinfo(av[1], av[2], &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(1);
	}

	int	sfd = -1;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue ;
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break ;
		close(sfd);
		sfd = -1;
	}

	freeaddrinfo(result);

	if (sfd == -1) {
		perror("couldn't bind");
		close(sfd);
		exit(1);
	}

	int	epollfd = epoll_create1(0);
	if (epollfd == -1) {
		perror("epollcreate1");
		close(sfd);
		exit(1);
	}

	struct epoll_event ev, events[10];

	ev.events = EPOLLOUT;
	ev.data.fd = sfd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
		perror("epoll_ctl ADD sfd");
		close(sfd);
		exit(1);
	}

	epoll_wait(epollfd, events, 10, -1);
	send(sfd, "GET / HTTP/1.0\r\n", 18, 0);
	ev.events = EPOLLHUP | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, sfd, &ev);
	epoll_wait(epollfd, events, 10, -1);

	close(epollfd);
	close(sfd);
	return (0);
}
