#include <linux/netlink.h>
#include <sys/poll.h> 
#include <sys/socket.h>

static void die(char *s)
{
	write(2, s, strlen(s));
	exit(1);
}

int main(int argc, char *argv[])
{
	struct sockaddr_nl nls;
	struct pollfd pfd;
	char buf[512];

	memset(&nls, 0, sizeof(struct sockaddr_nl));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();
	nls.nl_groups = -1;

	pfd.events = POLLIN;
	pfd.fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if(pfd.fd == -1) 
		die("Not root\n");
	
	if(bind(pfd.fd, (void*)&nls, sizeof(struct sockaddr_nl))) 
		die("Bind failed\n");

	while (-1 != poll(&pfd, 1, -1))
	{
		/* code */
		int i, len = recv(pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
		if(len == -1)
			die("recv\n");

		i = 0;
		while(1 < len) {
			printf("%s\n", buf + i);
			i += strlen(buf+i) + 1;
		}
	}
	die("poll\n");

	return 0;

}