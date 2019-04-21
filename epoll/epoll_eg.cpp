#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <cstdint>
#include <errno.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
 
/* epoll event callback function */
void evt_proc(struct epoll_event *trig_event);
/* epoll callback function pointer */
typedef void (*callback_fun)(epoll_event*);
/* write fd thread,trigger EPOLLIN event */
void *trig_thread(void *arg);
/* Because it is the edge trigger mode, you must ensure that all data is read every time until EAGAIN is returned. */
int readAll(int fd);
 
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

static bool  abort_flag = false;

void abort_loop(int sig)
{
	abort_flag= true;
	(void)signal(SIGINT,SIG_DFL);
}
 
int main(void)
{
	(void)signal(SIGINT,abort_loop);
	/* create a file descriptor for event notification */
	int evtfd=eventfd(0,0);
	
	/* open an epoll file descriptor */
	int epl_fd = epoll_create(1);
	if (epl_fd < 0)
	{
		perror("epoll create failed");
		exit(errno);
	}
 
	EpollData epldata;
	epldata.epoll_fd = epl_fd;
	epldata.listen_fd = evtfd;
	epldata.clb_func=evt_proc;
	epoll_event ep_ev;
	/*
	The associated fd is available for read operations,
	and sets the Edge Triggered behavior for the associated file descriptor
	*/
	ep_ev.events=EPOLLIN|EPOLLET;
	ep_ev.data.ptr=&epldata;
 
	/* Register the target file descriptor fd on the epoll instance */
	epoll_ctl(epl_fd, EPOLL_CTL_ADD, evtfd, &ep_ev);
 
	/* available events */
	epoll_event trig_event;
	/* the number of milliseconds that epoll_wait() will block */
	int timeout = 500;
 
	/* create thread to write evtfd */
	pthread_t a_thread;
	int crt_res=pthread_create(&a_thread,nullptr,trig_thread,(void*)&evtfd);
	if(crt_res!=0)
	{
		perror("create thread error");
	}
 
	int evt_wt_res=0;
	while (!abort_flag)
	{
		evt_wt_res = epoll_wait(epl_fd, &trig_event, 1, timeout);
		switch (evt_wt_res)
		{
		case 0:
			/* no file descriptor became ready */
			/*printf("epoll_wait timeout\n");*/
			break;
		case -1:
			/* some error occurred */
			perror("epoll_wait error");
			break;
		default:
			callback_fun pfnc=((EpollData*)(trig_event.data.ptr))->clb_func;
			(*pfnc)(&trig_event);
			break;
		}
	}
	pthread_join(a_thread,nullptr);
	close(epl_fd);
	close(evtfd);
	return 0;
}
 
void evt_proc(struct epoll_event *trig_event)
{
	int evnt = trig_event->events;
	EpollData *epldata = (EpollData*)trig_event->data.ptr;
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
	int result = 0;
	printf("recv msg:\n");
	uint64_t msg;
	result = read(fd, &msg,sizeof(uint64_t));
	if(result < 0)
	{
		perror("read fd error!\n");
	}
	else
	{
		printf("%d",msg);
	}
	printf("\n");

	return result;
}

void *trig_thread(void *arg)
{
	int fd=*(int*)arg;
	srand(time(nullptr));
	while(!abort_flag)
	{
		uint64_t num = rand()%100;
		int res=write(fd,&num,sizeof(uint64_t));
		if(res<0)
		{
			perror("write fd error");
		}
		sleep(1);
	}
	
	return nullptr;
}

