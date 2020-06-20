/*

*/

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

/* epoll事件回调函数 */
/* 处理新的TCP连接请求 */
void new_connect_proc(struct epoll_event *trig_event, std::unordered_set<void*> &pset);
/* 处理TCP数据接收 */
void new_data_proc(struct epoll_event *trig_event, std::unordered_set<void*> &pset);

/* 程序退出时释放资源 */
void abort_proc(std::unordered_set<void*> &pset);

/* epoll回调函数指针声明 */
typedef void (*callback_fun)(epoll_event*, std::unordered_set<void*> &);

/* epoll data */
struct EpollData
{
	/* epoll fd */
	int epoll_fd;
	/* 监听的fd */
	int listen_fd;
	/* 回调函数指针 */
	callback_fun clb_func;
};

/* 创建TCP监听套接字 */
int create_listenfd(const char *localIP, unsigned short port);
/* 将fd设置为非阻塞模式 */
int setfd_nonblock(int fd);
/* 因为使用的是边沿触发, 每次读取数据时必须不断尝试读取直到返回EAGAIN. */
int readAll(int fd);

static bool  abort_flag = false;

/* 信号处理函数, 处理SIGINT信号(Ctrl+C) */
void abort_loop(int sig)
{
	abort_flag = true;
	(void)signal(sig,SIG_DFL);
}

int main(int argc, char* argv[])
{
	/* 设置SIGINT信号处理函数,以便于按Ctrl+C退出 */
	(void)signal(SIGINT,abort_loop);
	/* 创建TCP监听套接字 */
	int listen_fd = create_listenfd("0.0.0.0", 8086);

	/* 创建epoll fd */
	int eplfd = epoll_create(1);
	if (eplfd < 0)
	{
		printf("%s,L%d,epoll create failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		exit(errno);
	}

	EpollData epldata;
	epldata.epoll_fd = eplfd;
	epldata.listen_fd = listen_fd;
	epldata.clb_func = new_connect_proc;
	epoll_event ep_ev;
	/*
	关心的时间类型为EPOLLIN(收到新的TCP连接请求会触发EPOLLIN事件)
	触发方式为边沿触发EPOLLET
	*/
	ep_ev.events = EPOLLIN|EPOLLET;
	/* User data指针指向EpollData, 当epoll就绪时就可以通过此指针获知就绪的fd及其回调函数 */
	ep_ev.data.ptr = &epldata;

	/* 将TCP监听socket添加到epoll */
	epoll_ctl(eplfd, EPOLL_CTL_ADD, listen_fd, &ep_ev);

	/* 用于存放就绪event的数组 */
	epoll_event trig_event[10];
	/* epoll_wait()最大阻塞等待的时间(即等待超时时间) */
	int ep_wait_tm = 1000;

	/* 用于为每个fd存储EpollData指针的unordered_set */
	/*
	由于客户端断开连接的时机时无序和不可预知的, 为了在TCP断开时快速找到fd对应的EpollData指针并释放内存,
	这里使用了STL中的unordered_set提高查找速度
	*/
	std::unordered_set<void*> eplDataPtrSet;
	int evt_wt_res = 0;
	while (!abort_flag)
	{
		memset(&trig_event, 0, sizeof(trig_event));
		evt_wt_res = epoll_wait(eplfd, trig_event, 10, ep_wait_tm);
		switch (evt_wt_res)
		{
		case 0:
			/* 没有fd就绪或者epoll_wait()等待超时 */
			break;
		case -1:
			/* epoll_wait发生错误 */
			printf("%s,L%d,epoll_wait error:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
			break;
		default:
			{
				/* 有fd就绪, 逐个处理就绪的fd */
				for(int i = 0;i<evt_wt_res;++i)
				{
					/* 通过之前关联的回调函数处理fd事件 */
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
			printf("%s,L%d,set listen socket to non-blocking failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		}
		/* addr重用, 非必须, 在程序重启时有用, 不进行此操作重启时可能报错端口号被占用 */
		int addrReuse = 1;
		if(-1 == setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &addrReuse, sizeof(int)))
		{
			printf("%s,L%d,set addr reuse failed!:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		}

		/* 绑定addr, 与普通TCP无差别 */
		if(-1 == bind(listenfd, (sockaddr *)&localAddr, socklen))
		{
			printf("%s,L%d,socket bind error:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
			close(listenfd);
			listenfd = -1;
		}
		else
		{
			/* 开始监听 */
			if(-1 == listen(listenfd, maxListenFd))
			{
				printf("%s,L%d,ocket listen error:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
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
	/* 将socket fd设置为非阻塞模式 */
    /*
    对于边缘触发模式, fd必须设置为非阻塞
    因为边缘触发模式每次触发时需要循环读取数据直到read()返回EAGAIN, errno为EWOULDBLOCK.
    如果设置为阻塞模式, 连续做read操作而没有数据可读, read()将会阻塞而不能立即返回, 会使程序阻塞, 
	而我们使用epoll就是为了IO复用避免阻塞
    */
	/* 先获取fd当前的flag */
	int flags = fcntl(fd, F_GETFL, 0);
	if(-1 != flags)
	{
		/* 在当前flag的基础上增加非阻塞标记 */
		if(-1 == fcntl(fd, F_SETFL, flags|O_NONBLOCK))
		{
			printf("%s,L%d,set socket flags to non-blocking failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
			result = -1;
		}
	}
	else
	{
		printf("%s,L%d,et socket flags failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		result = -1;
	}
	return result;
}

/*
有新的TCP连接事件的回调函数
在此函数中accept()连接并将它注册到epoll去监听
*/
void new_connect_proc(epoll_event *trig_event, std::unordered_set<void*> &pset)
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
			printf("%s,L%d,new connection accept failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		}
		else
		{
			/* 边缘触发模式记得将新accept的连接fd也设置为非阻塞模式 */
			if(-1 == setfd_nonblock(fd))
			{
				printf("%s,L%d,set acepted scoket to non-blocking failed:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
			}
			else
			{
				/* 在TCP连接断开和程序退出时, 记得释放此处申请的内存 */
				EpollData *_t_epldata = new EpollData();
				_t_epldata->epoll_fd = epldata->epoll_fd;
				_t_epldata->listen_fd = fd;
				_t_epldata->clb_func = new_data_proc;

				epoll_event ep_ev;
				/* 关心的事件有:
				有新的数据可读EPOLLIN,
				对端shutdown TCP连接, 即TCP的半关闭状态, EPOLLRDHUP
				双方都shutdown TCP连接,EPOLLRDHUP
				*/
				/*
				边缘触发模式EPOLLET
				*/
				ep_ev.events = EPOLLIN|EPOLLRDHUP|EPOLLHUP|EPOLLET;
				ep_ev.data.ptr = _t_epldata;

				if(-1 == epoll_ctl(epldata->epoll_fd, EPOLL_CTL_ADD, _t_epldata->listen_fd, &ep_ev))
				{
					printf("%s,L%d,add new accepted socket to epoll error:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
					/* fd注册失败需要释放刚才申请的资源 */
					delete _t_epldata;
				}
				else
				{
					/* 组测成功, 将指针添加到unordered_set, 便于后续释放资源 */
					pset.insert(_t_epldata);
					char greetmsg[] = "hello!I am server.";
					/* 向客户端发送问候语 */
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

/*
处理有新的数据或TCP断开事件的回调函数
在此函数中, 如果有新的数据可读, 则一次性读取所有数据
如果TCP连接断开, 则把它从epoll中移除, 关闭其fd, 从unordered_set中删除, 并释放连接建立时为其申请的EpollData内存
*/
void new_data_proc(epoll_event *trig_event, std::unordered_set<void*> &pset)
{
	int evnt = trig_event->events;
	EpollData *epldata = (EpollData*)(trig_event->data.ptr);
	if(epldata == nullptr)
	{
		return;
	}

	/* 对端shutdownTCP连接时, 会触发EPOLLIN和EPOLLRDHUP(内核2.6.17开始)事件 */
	if(evnt & (EPOLLHUP|EPOLLRDHUP))
	{
		/* EPOLL_CTL_DEL参数4会被忽略且可以是nullptr */
		/* 在内核2.6.9之前参数4必须是非空event指针, 虽然此参数在EPOLL_CTL_DEL操作中会被忽略, 这是早期内核的一个bug */
		if(-1 == epoll_ctl(epldata->epoll_fd, EPOLL_CTL_DEL, epldata->listen_fd, nullptr))
		{
			printf("%s,L%d,delete a log-off socket failed!%d,%s\n",__func__,__LINE__,errno, strerror(errno));
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
			printf("%s,L%d,epoll error!:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
		}
	}
}

int readAll(int fd)
{
	int read_count = 0, result = 0;
	char recvMsg[1024] = { 0 };
	/*
	循环读取直到read返回EAGAIN
	在此之前已经将fd设置为非阻塞模式, 没有数据可读时read()会返回EAGAIN, 并将errno设置为EWOULDBLOCK
	*/
    int t_errno = 0;
    bool break_loop = false;
	do
	{
		result = recv(fd, &recvMsg, sizeof(recvMsg)-1, 0);
        t_errno = errno;
		if(result < 0)
		{
            /* 实际应用时需要处理EINTR信号(read()操作可能会被中断打断) */
            switch(t_errno)
            {
            case EWOULDBLOCK:
				/* 已经没有数据可读 */
                break_loop = true;
                break;
            case EINTR:
				/* read()操作被中断打断, 需要继续读取 */
                break_loop = false;
                break;
            default:
				/* 其他错误 */
                break_loop = true;
				printf("%s,L%d,error in recv:%d,%s\n",__func__,__LINE__,errno, strerror(errno));
                break;
            }
		}
		else
		{
			read_count += result;
			recvMsg[result] = '\0';
			printf("%s",recvMsg);
			printf("\n");
		}
	}while(!break_loop);

	return read_count;
}

void abort_proc(std::unordered_set<void*> &pset)
{
	/* 逐一关闭socket并释放内存 */
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
