/* $Header: /afs/dev.mit.edu/source/repository/athena/etc/xdm/dm/dm.c,v 1.6 1990-11-15 18:30:26 mar Exp $
 *
 * Copyright (c) 1990 by the Massachusetts Institute of Technology
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 *
 * This is the top-level of the display manager and console control
 * for Athena's xlogin.
 */

#include <mit-copyright.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utmp.h>
#include <ctype.h>
#include <strings.h>


#ifndef lint
static char *rcsid_main = "$Header: /afs/dev.mit.edu/source/repository/athena/etc/xdm/dm/dm.c,v 1.6 1990-11-15 18:30:26 mar Exp $";
#endif

#ifndef NULL
#define NULL 0
#endif

#ifndef __STDC__
#define volatile
#endif

/* Process states */
#define NONEXISTANT	0
#define RUNNING		1
#define STARTUP		2
#define CONSOLELOGIN	3

#define FALSE		0
#define TRUE		(!FALSE)

/* flags used by signal handlers */
volatile int alarm_running = NONEXISTANT;
volatile int xpid, x_running = NONEXISTANT;
volatile int consolepid, console_running = NONEXISTANT;
volatile int console_tty = 0, console_failed = FALSE;
volatile int loginpid, login_running = NONEXISTANT;
volatile int clflag;

/* Programs */
#ifdef ultrix
char *login_prog = "/etc/athena/console-getty";
#else
char *login_prog = "/bin/login";
#endif /* ultrix */

/* Files */
char *utmpf =	"/etc/utmp";
char *wtmpf =	"/usr/adm/wtmp";
char *passwdf =	"/etc/passwd";
char *passwdtf ="/etc/ptmp";
char *xpidf = 	"/usr/tmp/X0.pid";
char *consolepidf = "/etc/console.pid";
char *dmpidf =	"/etc/dm.pid";
char *consolef ="/dev/console";
char *consolelog = "/usr/tmp/console.log";
char *mousedev = "/dev/mouse";
char *displaydev = "/dev/cons";
#ifdef ultrix
char *ultrixcons = "/dev/xcons";
#endif

/* the console process will run as daemon */
#define DAEMON 1

#define X_START_WAIT	30	/* wait up to 30 seconds for X to be ready */
#define X_STOP_WAIT	3	/* (seconds / 2) wait for graceful shutdown */
#define LOGIN_START_WAIT 60	/* wait up to 1 minute for Xlogin */
#define BUFSIZ		1024



/* Setup signals, start X, start console, start login, wait */

main(argc, argv)
int argc;
char **argv;
{
    void die(), child(), catchalarm(), xready(), setclflag(), shutdown();
    void loginready();
    char *logintty, *consoletty, *conf, *p, *number(), *getconf();
    char **xargv, **consoleargv = NULL, **loginargv, **parseargs();
    char line[16], buf[256];
    fd_set readfds;
    int pgrp, file, tries, console = TRUE, mask;

    if (argc != 4 &&
	(argc != 5 || strcmp(argv[3], "-noconsole"))) {
	message("usage: ");
	message(argv[0]);
	message(" configfile logintty [-noconsole] consoletty\n");
	console_login(NULL);
    }
    if (argc == 5) console = FALSE;

    /* parse argument lists */
    conf = argv[1];
#ifndef ultrix
    logintty = argv[2];
#else
    logintty = "console";
#endif /* ultrix */
    consoletty = argv[argc-1];
    p = getconf(conf, "X");
    if (p == NULL)
      console_login("dm: Can't find X command line\n");
    xargv = parseargs(p, NULL);
    if (console) {
	p = getconf(conf, "console");
	if (p == NULL)
	  console_login("dm: Can't find console command line\n");
	consoleargv = parseargs(p, NULL);
    }
    p = getconf(conf, "login");
    if (p == NULL)
      console_login("dm: Can't find login command line\n");
    loginargv = parseargs(p, logintty);

    /* Signal Setup */
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);	/* so that X pipe errors don't nuke us */
    signal(SIGFPE, shutdown);
    signal(SIGHUP, die);
    signal(SIGINT, die);
    signal(SIGTERM, die);
    signal(SIGCHLD, child);
    signal(SIGALRM, catchalarm);
    signal(SIGUSR2, setclflag);

    /* setup ttys */
    close(0);
    close(1);
    close(2);
    strcpy(line, "/dev/");
    strcat(line, consoletty);
    open(line, O_RDWR, 0622);
    dup2(0, 1);
    dup2(1, 2);
#ifndef BROKEN_CONSOLE_DRIVER
    /* Set the console characteristics so we don't lose later */
#ifdef TIOCCONS
    if (console)
      ioctl (0, TIOCCONS, 0);		/* Grab the console   */
#endif  /* TIOCCONS */
    setpgrp(0, pgrp=getpid());		/* Reset the tty pgrp */
    ioctl (0, TIOCSPGRP, &pgrp);
#endif

    /* save our pid file */
    if ((file = open(dmpidf, O_WRONLY|O_TRUNC|O_CREAT, 0644)) >= 0) {
	write(file, number(getpid()), strlen(number(getpid())));
	close(file);
    }

    /* Fire up X */
    for (tries = 0; tries < 3; tries++) {
#ifdef DEBUG
	message("Starting X\n");
#endif
	x_running = STARTUP;
	xpid = fork();
	switch (xpid) {
	case 0:
	    if(fcntl(2, F_SETFD, 1) == -1)
	      close(2);
	    sigsetmask(0);
	    /* ignoring SIGUSR1 will cause the server to send us a SIGUSR1
	     * when it is ready to accept connections
	     */
	    signal(SIGUSR1, SIG_IGN);
	    p = *xargv;
	    *xargv = "-";
	    execv(p, xargv);
	    message("dm: X server failed exec\n");
	    _exit(1);
	case -1:
	    message("dm: Unable to fork to start X server\n");
	    break;
	default:
	    signal(SIGUSR1, xready);
	    if ((file = open(xpidf, O_WRONLY|O_TRUNC|O_CREAT, 0644)) >= 0) {
		write(file, number(xpid), strlen(number(xpid)));
		close(file);
	    }
	    if (x_running == NONEXISTANT) break;
	    alarm(X_START_WAIT);
	    alarm_running = RUNNING;
#ifdef DEBUG
	    message("waiting for X\n");
#endif
	    sigpause(0);
	    if (x_running != RUNNING) {
		if (alarm_running == NONEXISTANT)
		  message("dm: Unable to start X\n");
		else
		  message("dm: X failed to become ready\n");
	    }
	    signal(SIGUSR1, SIG_IGN);
	}
	if (x_running == RUNNING) break;
    }
    alarm(0);
    if (x_running != RUNNING)
      console_login("Unable to start X, doing console login instead.\n");

    /* start up console */
    strcpy(line, "/dev/");
    strcat(line, logintty);
    if (console) start_console(line, consoleargv);
    /* had to use a different tty, make sure xlogin uses it too */
    if (strcmp(logintty, &line[5]))
      strcpy(logintty, &line[5]);

    /* Fire up the X login */
    for (tries = 0; tries < 3; tries++) {
#ifdef DEBUG
	message("Starting X Login\n");
#endif
	clflag = FALSE;
	login_running = STARTUP;
	signal(SIGUSR1, loginready);
	loginpid = fork();
	switch (loginpid) {
	case 0:
	    for (file = 0; file < getdtablesize(); file++)
	      close(file);
	    file = open("/dev/tty", O_RDWR|O_NDELAY);
	    if (file >= 0) {
		(void) ioctl(file, TIOCNOTTY, (char *)NULL);
		(void) close(file);
	    }
	    open("/dev/null", O_RDONLY, 0);
	    strcpy(line, "/dev/");
	    strcat(line, logintty);
	    open(line, O_WRONLY, 0);
	    dup2(1, 2);
	    /* chase away anyone else using this tty */
	    signal(SIGHUP, SIG_IGN);
	    vhangup();
	    sigsetmask(0);
	    /* ignoring SIGUSR1 will cause xlogin to send us a SIGUSR1
	     * when it is ready
	     */
	    signal(SIGUSR1, SIG_IGN);
	    execv(loginargv[0], loginargv);
	    message("dm: X login failed exec\n");
	    _exit(1);
	case -1:
	    message("dm: Unable to fork to start X login\n");
	    break;
	default:
	    alarm(LOGIN_START_WAIT);
	    alarm_running = RUNNING;
	    while (login_running == STARTUP && alarm_running == RUNNING)
	      sigpause(0);
	    if (login_running != RUNNING) {
		kill(loginpid, SIGKILL);
		if (alarm_running != NONEXISTANT)
		  message("dm: Unable to start Xlogin\n");
		else
		  message("dm: Xlogin failed to become ready\n");
	    }
	}
	if (login_running == RUNNING) break;
    }
    signal(SIGUSR1, SIG_IGN);
    alarm(0);
    if (login_running != RUNNING)
      console_login("Unable to start xlogin, doing console login instead.\n");

    /* main loop.  Wait for SIGCHLD, waking up every minute anyway. */
    sigblock(sigmask(SIGCHLD));
    while (1) {
#ifdef DEBUG
	message("waiting...\n");
#endif
	/* Wait for something to hapen */
	if (console_failed) {
	    /* if no console is running, we must copy bits from the console
	     * (master side of pty) to the real console to appear as black
	     * bar messages.
	     */
	    FD_ZERO(&readfds);
	    FD_SET(console_tty, &readfds);
	    mask = sigsetmask(0);
	    select(console_tty + 1, &readfds, NULL, NULL, NULL);
	    sigblock(mask);
	    if (FD_ISSET(console_tty, &readfds)) {
		file = read(console_tty, buf, sizeof(buf));
		write(1, buf, file);
	    }
	} else {
	    alarm(60);
	    sigpause(0);
	}

	if (login_running == STARTUP) {
	    sigsetmask(0);
	    console_login("Console login requested.\n");
	}
	if (console && console_running == NONEXISTANT)
	  start_console(line, consoleargv);
	if (login_running == NONEXISTANT || x_running == NONEXISTANT) {
	    sigsetmask(0);
	    cleanup(logintty);
	    _exit(0);
	}
    }
}


/* Start a login on the raw console */

console_login(msg)
char *msg;
{
    int pgrp, i, gracefull = FALSE, xfirst = TRUE, cfirst = TRUE;
    struct sgttyb mode;
    char *nl = "\r\n";

#ifdef DEBUG
    message("starting console login\n");
#endif

    for (i = 0; i < X_STOP_WAIT; i++) {
	if (login_running != NONEXISTANT && login_running != STARTUP)
	  kill(loginpid, SIGKILL);
	if (console_running != NONEXISTANT) {
	    if (cfirst)
	      kill(consolepid, SIGHUP);
	    else
	      kill(consolepid, SIGKILL);
	    cfirst = FALSE;	
	}
	if (x_running != NONEXISTANT) {
	    if (xfirst)
	      kill(xpid, SIGTERM);
	    else
	      kill(xpid, SIGKILL);
	    xfirst = FALSE;
	}

	/* wait 1 sec for children to exit */
	alarm(2);
	sigpause(0);

	if (x_running == NONEXISTANT &&
	    console_running == NONEXISTANT &&
	    (login_running == NONEXISTANT || login_running == STARTUP)) {
	    gracefull = TRUE;
	    break;
	}
    }

#ifndef BROKEN_CONSOLE_DRIVER
    setpgrp(0, pgrp=0);		/* We have to reset the tty pgrp */
    ioctl(0, TIOCSPGRP, &pgrp);
#ifdef TIOCCONSx
    ioctl (0, TIOCCONS, 0);		/* Grab the console   */
    ioctl (1, TIOCCONS, 0);		/* Grab the console   */
#endif  /* TIOCCONS */
#endif

#ifdef vax
    /* attempt to reset the display head */
    if (!gracefull) {
	i = open(mousedev, O_RDWR, 0);
	if (i >= 0) {
	    alarm(2);
	    sigpause(0);
	    close(i);
	}
	i = open(displaydev, O_RDWR, 0);
	if (i >= 0) {
	    alarm(2);
	    sigpause(0);
	    close(i);
	}
    }
#endif

    ioctl(0, TIOCGETP, &mode);
    mode.sg_flags = mode.sg_flags & ~RAW | ECHO;
    ioctl(0, TIOCSETP, &mode);
    sigsetmask(0);

    if (msg)
      message(msg);
    else
      message(nl);
    execl(login_prog, login_prog, 0);
    message("dm: Unable to start console login\n");
    _exit(1);
}


/* start the console program.  It will have stdin set to the controling
 * side of the console pty, and stdout set to /dev/console inherited 
 * from the display manager.
 */

start_console(line, argv)
char *line;
char **argv;
{
    static struct timeval last_try = { 0, 0 };
    struct timeval now;
    int file, pgrp, i;
    char *number(), c;

#ifdef DEBUG
    message("Starting Console\n");
#endif

    if (console_tty == 0) {
#ifndef ultrix
	/* Open master side of pty */
	line[5] = 'p';
	console_tty = open(line, O_RDWR, 0);
#else
	console_tty = open(ultrixcons, O_RDWR, 0);
#endif /* ultrix */
	if (console_tty < 0) {
	    /* failed to open this tty, find another one */
	    for (c = 'p'; c <= 's'; c++) {
		line[8] = c;
		for (i = 0; i < 16; i++) {
		    line[9] = "0123456789abcdef"[i];
		    console_tty = open(line, O_RDWR, 0);
		    if (console_tty >= 0) break;
		}
		if (console_tty >= 0) break;
	    }
	}
	/* out of ptys, use stdout (/dev/console) */
	if (console_tty < 0) console_tty = 1;
	/* Create console log file owned by daemon */
	if (access(consolelog, F_OK) != 0) {
	    file = open(consolelog, O_CREAT, 0644);
	    close(file);
	}
	chown(consolelog, DAEMON, 0);
    }
#ifdef TIOCCONS
    ioctl (console_tty, TIOCCONS, 0);		/* Grab the console   */
#endif /* TIOCCONS */
    line[5] = 't';

    gettimeofday(&now, 0);
    if (now.tv_sec <= last_try.tv_sec + 3) {
	/* giveup on console */
#ifdef DEBUG
	message("Giving up on console\n");
#endif
#ifndef BROKEN_CONSOLE_DRIVER
	/* Set the console characteristics so we don't lose later */
#ifdef TIOCCONS
	ioctl (0, TIOCCONS, 0);		/* Grab the console   */
#endif /* TIOCCONS */
	setpgrp(0, pgrp=getpid());		/* Reset the tty pgrp */
	ioctl (0, TIOCSPGRP, &pgrp);
#endif
	console_failed = TRUE;
	return;
    }
    last_try.tv_sec = now.tv_sec;


    console_running = RUNNING; 
    consolepid = fork();
    switch (consolepid) {
    case 0:
	/* Close all file descriptors except stdout/stderr */
	close(0);
	for (file = 3; file < getdtablesize(); file++)
	  if (file != console_tty)
	    close(file);
	file = open("/dev/tty", O_RDWR|O_NDELAY);
	if (file >= 0) {
	    (void) ioctl(file, TIOCNOTTY, (char *)NULL);
	    (void) close(file);
	}
	dup2(console_tty, 0);
	close(console_tty);
#ifdef DEBUG
	close(1);
	close(2);
	open("/tmp/console.err", O_CREAT|O_APPEND|O_WRONLY, 0644);
	dup2(1, 2);
#endif
	setreuid(DAEMON, DAEMON);
	sigsetmask(0);
	execv(argv[0], argv);
	message("dm: Failed to exec console\n");
	_exit(1);
    case -1:
	message("dm: Unable to fork to start console\n");
	_exit(1);
    default:
	if ((file = open(consolepidf, O_WRONLY|O_TRUNC|O_CREAT, 0644)) >= 0) {
	    write(file, number(consolepid), strlen(number(consolepid)));
	    close(file);
	}
    }
}


/* Kill children and hang around forever */

void shutdown()
{
    if (login_running == RUNNING)
      kill(loginpid, SIGHUP);
    if (console_running == RUNNING)
      kill(consolepid, SIGHUP);
    if (x_running == RUNNING)
      kill(xpid, SIGKILL);
    while (1) sigpause(0);
}


/* Kill children, remove password entry, kdestroy */

cleanup(tty)
char *tty;
{
    int file, found;
    struct utmp utmp;    
    char login[9];
    char tkt_file[64];

    if (login_running == RUNNING)
      kill(loginpid, SIGHUP);
    if (console_running == RUNNING)
      kill(consolepid, SIGHUP);
    if (x_running == RUNNING)
      kill(xpid, SIGKILL);

    strcpy(tkt_file, "/tmp/tkt_");
    strcat(tkt_file, tty);
    kdestroy(tkt_file);

    found = 0;
    if ((file = open(utmpf, O_RDWR, 0)) >= 0) {
	while (read(file, (char *) &utmp, sizeof(utmp)) > 0) {
	    if (!strncmp(utmp.ut_line, tty, sizeof(utmp.ut_line))) {
		strncpy(login, utmp.ut_name, 8);
		login[8] = 0;
		utmp.ut_name[0] = 0;
		lseek(file, (long) -sizeof(utmp), L_INCR);
		write(file, (char *) &utmp, sizeof(utmp));
		found = 1;
	    }
	}
	close(file);
    }
    if (found) {
	if ((file = open(wtmpf, O_WRONLY|O_APPEND, 0644)) >= 0) {
	    strcpy(utmp.ut_line, tty);
	    utmp.ut_name[0] = 0;
	    utmp.ut_host[0] = 0;
	    time(&utmp.ut_time);
	    write(file, (char *) &utmp, sizeof(utmp));
	    close(file);
	}
    }

    if (clflag) {
	/* Clean up password file */
	removepwent(login);
    }
}


/* When we get sigchild, figure out which child it was and set
 * appropriate flags
 */

void child()
{
    int pid;
    union wait status;
    char *number();

    pid = wait3(&status, WNOHANG, 0);
    if (pid == 0) return;

#ifdef DEBUG
    message("Child exited "); message(number(pid));
#endif
    if (pid == xpid) {
#ifdef DEBUG
	message("X Server exited\n");
#endif
#ifdef TRACE
	trace("X Server exited status ");
	trace(number(status.w_retcode));
#endif
	x_running = NONEXISTANT;
    } else if (pid == consolepid) {
#ifdef DEBUG
	message("Console exited\n");
#endif
#ifdef TRACE
	trace("Console exited status ");
	trace(number(status.w_retcode));
#endif
	console_running = NONEXISTANT;
    } else if (pid == loginpid) {
#ifdef DEBUG
	message("X Login exited\n");
#endif
#ifdef TRACE
	trace("X Login exited status ");
	trace(number(status.w_retcode));
#endif
	if (status.w_retcode == CONSOLELOGIN)
	  login_running = STARTUP;
	else
	  login_running = NONEXISTANT;
    } else {
	message("dm: Unexpected SIGCHLD from pid ");message(number(pid));
    }
}


void xready()
{
#ifdef DEBUG
    message("X Server ready\n");
#endif
    x_running = RUNNING;
}

void loginready()
{
#ifdef DEBUG
    message("X Login ready\n");
#endif
    login_running = RUNNING;
}

void setclflag()
{
#ifdef DEBUG
    message("Received Clear Login Flag signal\n");
#endif
    clflag = TRUE;
}


/* When an alarm happens, just note it and return */

void catchalarm()
{
#ifdef DEBUG
    message("Alarm!\n");
#endif
    alarm_running = NONEXISTANT;
}


/* kill children and go away */

void die()
{
#ifdef DEBUG
    message("Killing children and exiting\n");
#endif
    if (login_running == RUNNING)
      kill(loginpid, SIGHUP);
    if (console_running == RUNNING)
      kill(consolepid, SIGHUP);
    if (x_running == RUNNING)
      kill(xpid, SIGKILL);
    _exit(0);
}


kdestroy(file)
char *file;
{
    int i, fd;
    struct stat statb;
    char buf[BUFSIZ];

    if (lstat(file,&statb) < 0) return;
    if (!(statb.st_mode & S_IFREG)) return;
    bzero(buf, BUFSIZ);

    if ((fd = open(file, O_RDWR, 0)) < 0) return;

    for (i = 0; i < statb.st_size; i += BUFSIZ)
	if (write(fd, buf, BUFSIZ) != BUFSIZ) {
	    (void) fsync(fd);
	    (void) close(fd);
	    return;
	}

    (void) fsync(fd);
    (void) close(fd);
    (void) unlink(file);
}


/* Remove a password entry.  Scans the password file for the specified
 * entry, and if found removes it.
 */

removepwent(login)
char *login;
{
    int count = 10;
    int newfile, oldfile, cc;
    char buf[BUFSIZ], *p, *start;

    if (!strcmp(login, "root")) return;

    while ((access(passwdtf, F_OK) == 0) && --count) sleep(1);
    if (count == 0) unlink(passwdtf);

    oldfile = open(passwdf, O_RDONLY, 0);
    if (oldfile < 0) return;
    newfile = open(passwdtf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (newfile < 0) {
	close(oldfile);
	return;
    }

    /* process each line of file */
    cc = read(oldfile, buf, BUFSIZ);
    while (1) {
	start = index(buf, '\n');
	if (start == NULL || start > &buf[cc]) break;
	start++; /* pointing at start of next line */
	if (strncmp(buf, login, strlen(login)) ||
	    buf[strlen(login)] != ':') {
	    write(newfile, buf, start - buf);
	}
	cc -= start - buf;
	/* don't use lib routine to make sure it works with overlapping copy */
	/* bcopy(buf, start, cc); */
	for (p = buf; p != &buf[cc]; p++) *p = p[start - buf];
	cc += read(oldfile, &buf[cc], BUFSIZ - cc);
    }
    close(newfile);
    close(oldfile);
    rename(passwdtf, passwdf);
}


/* Takes a command line, returns an argv-style  NULL terminated array 
 * of strings.  The array is in malloc'ed memory.
 */

char **parseargs(line, extra)
char *line;
char *extra;
{
    int i = 0;
    char *p = line;
    char **ret;
    char *malloc();

    while (*p) {
	while (*p && isspace(*p)) p++;
	while (*p && !isspace(*p)) p++;
	i++;
    }
    ret = (char **) malloc(sizeof(char *) * (i + 2));

    p = line;
    i = 0;
    while (*p) {
	while (*p && isspace(*p)) p++;
	if (*p == 0) break;
	ret[i++] = p;
	while (*p && !isspace(*p)) p++;
	if (*p == 0) break;
	*p++ = 0;
    }
    if (extra)
      ret[i++] = extra;
    ret[i] = NULL;
    return(ret);
}


/* Used for logging errors and other messages so we don't need to link
 * against the stdio library.
 */

message(s)
char *s;
{
    int i = strlen(s);
    write(2, s, i);
}


/* Convert an int to a string, and return a pointer to this string in a
 * static buffer.  The string will be newline and null terminated.
 */

char *number(x)
int x;
{
#define NDIGITS 16
    static char buffer[NDIGITS];
    char *p = &buffer[NDIGITS-1];

    *p-- = 0;
    *p-- = '\n';
    while (x) {
	*p-- = x % 10 + '0';
	x = x / 10;
    }
    if (p == &buffer[NDIGITS-3])
      *p-- = '0';
    return(p+1);
}


/* Find a named field in the config file.  Config file contains 
 * comment lines starting with #, and lines with a field name,
 * whitespace, then field value.  This routine returns the field
 * value, or NULL on error.
 */

char *getconf(file, name)
char *file;
char *name;
{
    static char buf[1024];
    static int inited = 0;
    char *p, *ret, *malloc();
    int i;

    if (!inited) {
	int fd;

	fd = open(file, O_RDONLY, 0644);
	if (fd < 0) return(NULL);
	i = read(fd, buf, sizeof(buf));
	buf[i] = 0;
	close(fd);
	inited = 1;
    }

    for (p = &buf[0]; p && *p; p = index(p, '\n')) {
	if (*p == '\n') p++;
	if (p == NULL || *p == 0) return(NULL);
	if (*p == '#') continue;
	if (strncmp(p, name, strlen(name))) continue;
	p += strlen(name);
	if (*p && !isspace(*p)) continue;
	while (*p && isspace(*p)) p++;
	if (*p == 0) return(NULL);
	ret = index(p, '\n');
	if (ret)
	  i = ret - p;
	else
	  i = strlen(p);
	ret = malloc(i+1);
	bcopy(p, ret, i+1);
	return(ret);
    }
    return(NULL);
}


#ifdef TRACE
trace(msg)
char *msg;
{
    int f;

    f = open("/tmp/dm.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (!f) return;
    write(f, msg, strlen(msg));
    close(f);
}
#endif
