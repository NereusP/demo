#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <string.h>
#include <cstdint>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/* epoll event callback function */
/* Handling new connection requests */
void newconnect_proc(struct epoll_event *trig_event, std::unordered_set<void*> &pset);
/* There is new data to receive */
void newdata_proc(struct epoll_event *trig_event, std::unordered_set<void*> &pset);

/* Free memory when program exits */
void abort_proc(std::unordered_set<void*> &pset);

/* epoll callback function pointer */
typedef void (*callback_fun)(epoll_event*, std::unordered_set<void*> &);

/* epoll data */
struct EpollData
{
	/* epoll fd */
	int epoll_fd;
	/* listen file descriptor */
	int listen_fd;
	/* callback pointer */
	callback_fun clb_func;
};

/* Create a TCP listening socket */
int create_listenfd(const char *localIP, unsigned short port);
/* Set fd to non-blocking mode */
int setfd_nonblock(int fd);
/* Because it is the edge trigger mode, you must ensure that all data is read every time until EAGAIN is returned. */
int readAll(int fd);

static bool  abort_flag = false;

/* Signal SIGINT handler */
void abort_loop(int sig)
{
	abort_flag = true;
	(void)signal(sig,SIG_DFL);
}

int main(void)
{
	/* Press ctrl+c to exit */
	(void)signal(SIGINT,abort_loop);
	/* create a file descriptor for event notification */
	int listen_fd = create_listenfd("0.0.0.0", 8086);

	/* open an epoll file descriptor */
	int eplfd = epoll_create(1);
	if (eplfd < 0)
	{
		perror("epoll create failed");
		exit(errno);
	}

	EpollData epldata;
	epldata.epoll_fd = eplfd;
	epldata.listen_fd = listen_fd;
	epldata.clb_func = newconnect_proc;
	epoll_event ep_ev;
	/*
	The associated fd is available for read operations,
	and sets the Edge Triggered behavior for the associated file descriptor
	*/
	ep_ev.events = EPOLLIN|EPOLLET;
	ep_ev.data.ptr = &epldata;

	/* Add TCP listening socket to epoll */
	epoll_ctl(eplfd, EPOLL_CTL_ADD, listen_fd, &ep_ev);

	/* available events */
	epoll_event trig_event[10];
	/* the number of milliseconds that epoll_wait() will block */
	int timeout = 1000;

	std::unordered_set<void*> eplDataPtrSet;
	int evt_wt_res = 0;
	while (!abort_flag)
	{
		memset(&trig_event, 0, sizeof(trig_event));
		evt_wt_res = epoll_wait(eplfd, trig_event, 10, timeout);
		switch (evt_wt_res)
		{
		case 0:
			/* no file descriptor became ready/epoll_wait timeout */
			break;
		case -1:
			/* some error occurred */
			perror("w:epoll_wait error");
			break;
		default:
			{
				for(int i = 0;i<evt_wt_res;++i)
				{
					callback_fun pfnc=((EpollData*)(trig_event[i].data.ptr))->clb_func;
					(*pfnc)(&trig_event[i], eplDataPtrSet);
				}
			}
			break;
		}
	}
	close(eplfd);
	abort_proc(eplDataPtrSet);
	close(listen_fd);
	return 0;
}

int create_listenfd(const char *localIP, unsigned short port)
{
	int maxListenFd = 5;
	sockaddr_in localAddr;
	memset(&localAddr, 0, sizeof(localAddr));
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = inet_addr(localIP);
	localAddr.sin_port = htons(port);
	socklen_t socklen = sizeof(localAddr);

	int listenfd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
	if(listenfd != -1)
	{

		if(-1 == setfd_nonblock(listenfd))
		{
			perror("set listen socket to non-blocking failed!");
		}
		/* Reuse Addr */
		int addrReuse = 1;
		if(-1 == setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &addrReuse, sizeof(int)))
		{
			perror("w:get addr reuse failed!");
		}

		/* bind addr */
		if(-1 == bind(listenfd, (sockaddr *)&localAddr, socklen))
		{
			perror("e:socket bind error!");
			close(listenfd);
			listenfd = -1;
		}
		else
		{
			if(-1 == listen(listenfd, maxListenFd))
			{
				perror("e:socket listen error!");
				close(listenfd);
				listenfd = -1;
			}
		}
	}

	return listenfd;
}

int setfd_nonblock(int fd)
{
	int result = 0;
	/* set socket flags to non-blocking */
	int flags = fcntl(fd, F_GETFL, 0);
	if(-1 != flags)
	{
		if(-1 == fcntl(fd, F_SETFL, flags|O_NONBLOCK))
		{
			perror("e:set socket flags to non-blocking failed!");
			result = -1;
		}
	}
	else
	{
		perror("e:get socket flags failed!");
		result = -1;
	}
	return result;
}

void newconnect_proc(epoll_event *trig_event, std::unordered_set<void*> &pset)
{
	if (trig_event->events & EPOLLIN)
	{
		sockaddr_in clientAddr;
		socklen_t addrLen = sizeof(clientAddr);
		memset(&clientAddr, 0, sizeof(clientAddr));

		EpollData *epldata = (EpollData *)(trig_event->data.ptr);
		int fd = accept(epldata->listen_fd, (sockaddr *)&clientAddr, &addrLen);
		if(fd == -1)
		{
			perror("new connection accept failed!");
		}
		else
		{
			if(-1 == setfd_nonblock(fd))
			{
				perror("set acepted scoket to non-blocking failed!");
			}
			else
			{
				/* Remember to release the memory allocated here when the TCP connection is broken or the program exits */
				EpollData *_t_epldata = new EpollData();
				pset.insert(_t_epldata);
				_t_epldata->epoll_fd = epldata->epoll_fd;
				_t_epldata->listen_fd = fd;
				_t_epldata->clb_func = newdata_proc;

				epoll_event ep_ev;
				ep_ev.events = EPOLLIN|EPOLLRDHUP|EPOLLHUP|EPOLLET;
				ep_ev.data.ptr = _t_epldata;

				if(-1 == epoll_ctl(epldata->epoll_fd, EPOLL_CTL_ADD, _t_epldata->listen_fd, &ep_ev))
				{
					perror("add new accepted socket to epoll error!");
				}
				else
				{
					char greetmsg[] = "hello!I am server.";
					send(_t_epldata->listen_fd, greetmsg, sizeof(greetmsg),0);
				}
			}
		}
	}
	else
	{
		printf("unknown event!\n");
	}
}

void newdata_proc(epoll_event *trig_event, std::unordered_set<void*> &pset)
{
	int evnt = trig_event->events;
	EpollData *epldata = (EpollData*)(trig_event->data.ptr);
	if(epldata == nullptr)
	{
		return;
	}

	/* EPOLLIN and EPOLLRDHUP will are triggered when client closes the connection */
	if(evnt & (EPOLLHUP|EPOLLRDHUP))
	{
		/* parameter 4 will be ignored and can be nullptr, except in kernel versions before 2.6.9. */
		if(-1 == epoll_ctl(epldata->epoll_fd, EPOLL_CTL_DEL, epldata->listen_fd, nullptr))
		{
			perror("delete a log-off socket failed!");
		}
		else
		{
			close(epldata->listen_fd);
			pset.erase(pset.find(epldata));
			delete epldata;
			epldata = nullptr;
			printf("a socket disconnected.\n");
		}
	}
	else
	{
		if (evnt & EPOLLIN)
		{
			readAll(epldata->listen_fd);
		}
		if (evnt & EPOLLERR)
		{
			perror("epoll error!");
		}
	}
}

int readAll(int fd)
{
	int read_count = 0, result = 0;
	char recvMsg[1024] = { 0 };
	printf("recv msg:\n");
	/* You need to continuously read/write a file descriptor untilEAGAIN when using the EPOLLET flag */
	do
	{
		result = recv(fd, &recvMsg, sizeof(recvMsg)-1, 0);
		if(result < 0)
		{
			if(errno != EAGAIN)
			{
				perror("read fd error!\n");
			}
		}
		else
		{
			read_count += result;
			recvMsg[result] = '\0';
			printf("%s",recvMsg);
		}
	}while(result > 0);
	printf("\n");

	return read_count;
}

void abort_proc(std::unordered_set<void*> &pset)
{
	while(pset.size() > 0)
	{
		auto it = pset.begin();
		EpollData *epldata = (EpollData*)(*it);
		if(epldata != nullptr)
		{
			close(epldata->listen_fd);
			delete epldata;
			epldata = nullptr;
		}
		pset.erase(it);
	}
}
