/* $Header: /afs/dev.mit.edu/source/repository/athena/etc/xdm/xlogin/timeout.c,v 1.8 1994-05-02 11:01:00 vrt Exp $ */

#include <mit-copyright.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef _AIX
#define _POSIX_SOURCE
#undef _BSD
#endif
#include <sys/wait.h>
#ifdef _AIX
#undef _POSIX_SOURCE
#endif

#ifndef FALSE
#define FALSE 0
#define TRUE (!FALSE)
#endif

#ifndef __STDC__
#define volatile
#endif

int app_pid;
volatile int app_running;
int app_exit_status;
main(argc, argv)
int argc;
char **argv;
{
    int maxidle, ttl;
    char *name, msg[512];
    struct stat stbuf;
    struct timeval now, start;
    void child(), wakeup();
#ifdef POSIX
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
#endif

    name = *argv++;
    if (argc < 3) {
	fprintf(stderr, "usage: %s max-idle-time application command line...\n",
		name);
	exit(1);
    }
    maxidle = atoi(*argv++);
    argc -= 2;

    app_running = TRUE;
#ifdef POSIX
     sigact.sa_handler = child;
    sigaction(SIGCHLD, &sigact, NULL);
    sigact.sa_handler = wakeup;
    sigaction(SIGALRM, &sigact, NULL);
#else
    signal(SIGCHLD, child);
    signal(SIGALRM, wakeup);
#endif
    /* launch application */
    switch (app_pid = fork()) {
    case 0:
	execvp(argv[0], argv);
	sprintf(msg, "%s: failed to exec application %s\n", name, argv[0]);
	perror(msg);
	exit(1);
    case -1:
	sprintf(msg, "%s: failed to fork to create application\n", name);
	perror(msg);
	exit(1);
    default:
	break;
    }

    gettimeofday(&start, NULL);
    ttl = maxidle;

    /* wait for application to die or idle-time to be reached */
    while (app_running) {
	alarm(ttl);
	sigpause(0);
	if (!app_running)
	  break;
	fstat(1, &stbuf);
	gettimeofday(&now, NULL);
	ttl = start.tv_sec + maxidle - now.tv_sec;
	/* only check idle time if we've been running at least that long */
	if (ttl <= 0)
	  ttl = stbuf.st_atime + maxidle - now.tv_sec;
	if (ttl <= 0) {
	    fprintf(stderr, "\nMAX IDLE TIME REACHED.\n");
	    kill(app_pid, SIGINT);
	    exit(0);
	}
    }
    exit(app_exit_status);
}

void child()
{
#ifdef POSIX
    int status;
#else
    union wait status;
#endif
    int pid;

#ifdef POSIX
    pid = waitpid(-1, &status, WNOHANG);
#else
    pid = wait3(&status, WNOHANG, 0);
#endif
    if (pid != app_pid || WIFSTOPPED(status))
      return;
    app_running = FALSE;
    if (WIFEXITED(status))
#ifdef POSIX
      app_exit_status =  WEXITSTATUS(status);
    else
      app_exit_status = WTERMSIG(status);
#else
      app_exit_status = status.w_retcode;
    else
      app_exit_status = -status.w_termsig;
#endif
}

void wakeup()
{
}
