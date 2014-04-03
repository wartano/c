#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <pthread.h>


#define UNIX_DOMAIN "/tmp/daemon.sock"


void signal_handler(int);
void daemonShutdown(void);
static void daemonize(const char *);
int serv_listen(void);
int *serv_accept(int);
void *connection_handler(void *);

int pidFilehandle = -1;
FILE *fp = NULL;


void signal_handler(int sig)
{
	switch(sig) {
        case SIGHUP:
            break;
        case SIGINT:
        case SIGTERM:
            daemonShutdown();
			      exit(EXIT_SUCCESS);
            break;
        default:
            break;
    }
}

void daemonShutdown()
{
	if (NULL != fp)
		fclose(fp);

	if (pidFilehandle != -1)
		close(pidFilehandle);

	unlink(UNIX_DOMAIN);
}


static void daemonize(const char *pidfile)
{
	  struct sigaction newSigAction;
    sigset_t newSigSet;
    pid_t process_id = 0;
	  char str[10];
	  int i;

	  /* Set up a signal handler */
    sigemptyset(&newSigAction.sa_mask);
    newSigAction.sa_flags = 0;
    newSigAction.sa_handler = signal_handler;

    /* Signals to handle */
    sigaction(SIGHUP, &newSigAction, NULL);     /* catch hangup signal */
    sigaction(SIGTERM, &newSigAction, NULL);    /* catch term signal */
    sigaction(SIGINT, &newSigAction, NULL);     /* catch interrupt signal */


	  /* Set signal mask - signals we want to block */
    sigemptyset(&newSigSet);
    sigaddset(&newSigSet, SIGCHLD);  /* ignore child - i.e. we don't need to wait for it */
    sigaddset(&newSigSet, SIGTSTP);  /* ignore Tty stop signals */
    sigaddset(&newSigSet, SIGTTOU);  /* ignore Tty background writes */
    sigaddset(&newSigSet, SIGTTIN);  /* ignore Tty background reads */
    sigprocmask(SIG_BLOCK, &newSigSet, NULL);   /* Block the above specified signals */

    
    if (chdir("/") < 0)
        exit(EXIT_FAILURE);

    umask(0);

	  // close all descriptors
    for (i = getdtablesize(); i >= 0; --i)
        close(i);

    /* Route I/O connections */

    /* Open STDIN */
    i = open("/dev/null", O_RDWR);

    /* STDOUT */
	  dup(i);

    /* STDERR */
    dup(i);

    process_id = fork();

    if (process_id < 0)
        exit(EXIT_FAILURE);
    else if (process_id > 0)
        exit(EXIT_SUCCESS);

    //set new session
    if (setsid() < 0)
        exit(EXIT_FAILURE);

	  /* Ensure only one copy */
    pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);

    if (pidFilehandle == -1 ) {
        /* Couldn't open lock file */
        exit(EXIT_FAILURE);
    }

    /* Try to lock file */
    if (lockf(pidFilehandle, F_TLOCK, 0) == -1) {
        /* Couldn't get lock on lock file */
        exit(EXIT_FAILURE);
    }

    /* Get and format PID */
    sprintf(str, "%d\n", getpid());

    /* write pid to lockfile */
    write(pidFilehandle, str, strlen(str));
}

int serv_listen()
{
	int fd;
	struct sockaddr_un un;

	/* create a UNIX domain stream socket */
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		fprintf(fp, "Cannot create a listening socket");
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	
	/* fill in socket address structure */
	memset(&un, 0, sizeof(struct sockaddr_un));
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, UNIX_DOMAIN);
	unlink(UNIX_DOMAIN);   /* in case it already exists */

	/* bind the name to the descriptor */
	if (bind(fd, (struct sockaddr *)&un, sizeof(struct sockaddr_un)) < 0) {
		fprintf(fp, "Cannot bind the server socket");
		goto errout;
	}
	if (listen(fd, 10) < 0) { /* tell kernel we're a server */
		fprintf(fp, "Cannot listen on the socket");
		goto errout;
	}
	return fd;

	errout:
		close(fd);
		daemonShutdown();
		exit(EXIT_FAILURE);
}

int *serv_accept(int listenfd)
{
	int *clifdp = malloc(sizeof(int));
	socklen_t len;
	struct sockaddr_un un;

	len = sizeof(struct sockaddr_un);
	if ((*clifdp = accept(listenfd, (struct sockaddr *)&un, &len)) < 0) {
		if (errno != EINTR) {
			free(clifdp);
			close(listenfd);
			daemonShutdown();
			exit(EXIT_FAILURE);
		}
	}

	return clifdp;
}


void *connection_handler(void *clifdp)
{
	pthread_detach(pthread_self());

	int sock_fd = *(int *)clifdp;
	free(clifdp);

	int recv_num;
	static char recv_buf[1024];


	memset(recv_buf, 0, sizeof(recv_buf));
	recv_num = read(sock_fd, recv_buf, sizeof(recv_buf));
	prnitf("%d\n", recv_num);
	printf("%s\n", recv_buf);
	
	return NULL;
}


int main(void)
{
    daemonize("/tmp/daemon.pid");

	  int fd;
	  pthread_t thread_id;

    fp = fopen("/tmp/daemon.log", "w");
    if (NULL == fp) {
		  daemonShutdown();
		  exit(EXIT_FAILURE);
	  }
	  
	  setbuf(fp, NULL);

    fd = serv_listen();
    
    while (1) {
		  int *clifdp;
		  clifdp = serv_accept(fd);

		  // Continue if accept() is interrupted by signals 
		  if (errno == EINTR) {
			  free(clifdp);
			  continue;
		  }

		  if (pthread_create(&thread_id, NULL, connection_handler, (void *) clifdp) < 0) {
			  fprintf(fp, "Failed to create a new thread\n");
			  free(clifdp);
			  daemonShutdown();
			  exit(EXIT_FAILURE);   
		  }
    }

    return 0;
}
