/* $Header: /afs/dev.mit.edu/source/repository/athena/etc/xdm/xlogin/xlogin.c,v 1.69 1998-02-02 22:25:08 ghudson Exp $ */
 
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <utmp.h>
#include <fcntl.h>
#include <X11/Intrinsic.h>
#include <ctype.h>
#include <errno.h>
#include <X11/Wc/WcCreate.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Text.h>
#include <X11/Xaw/Form.h>
#ifdef SOLARIS
#include <X11/Xaw/SmeBSB.h>
#endif
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xmu/Converters.h>
#include "Clock.h"
#include "owl.h"
#include "environment.h"

#ifdef SYSV
#define random	lrand48
#define srandom	srand48
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MOTD_FILENAME
#define MOTD_FILENAME "/afs/athena.mit.edu/system/config/motd/login.77"
#endif

#ifdef sgi
char athconsole[64];
FILE *xdmstream;
int xdmfd;
#endif

#define OWL_AWAKE 0
#define OWL_SLEEPY 1

#define OWL_STATIC 0
#define OWL_BLINKINGCLOSED 1
#define OWL_BLINKINGOPEN 2
#define OWL_SLEEPING 3
#define OWL_WAKING 4

#define ACTIVATED 1
#define REACTIVATING 2

#define DAEMON 1			/* UID for scripts to run as */
#define N_DAEMON "daemon"

gid_t def_grplist[] = { 101 };		/* default group list */


/* Function declarations. */

extern void AriRegisterAthena ();
static void move_instructions(), screensave(), unsave(), start_reactivate();
static void blinkOwl(), blinkIs(), initOwl(), adjustOwl();
static void catch_child(), setFontPath();
static Boolean auxConditions();
extern pid_t fork_and_store(pid_t *var);
void focusACT(), unfocusACT(), runACT(), runCB(), focusCB(), resetCB();
void idleReset(), loginACT(), localErrorHandler(), setcorrectfocus();
void sigconsACT(), sigconsCB(), callbackACT(), attachandrunCB();
#ifdef sgi
void windowShutdownACT(), windowShutdownCB();
#endif
extern void add_converter ();


/* Definition of the Application resources structure. */

typedef struct _XLoginResources {
  int save_timeout;
  int move_timeout;
  int blink_timeout;
  int reactivate_timeout;
  int activate_timeout;
  int restart_timeout;
  int randomize;
  int detach_interval;
  String activate_prog;
  String reactivate_prog;
  String tty;
  String session;
  String fontpath;
  String srvdcheck;
  String loginName;
  Boolean blankAll;
  Boolean showMotd;
  String motdFile;
  String motd2File;
} XLoginResources;

/* Command line options table.  Only resources are entered here...there is a
 * pass over the remaining options after XtParseCommand is let loose. 
 */

static XrmOptionDescRec options[] = {
  {"-save",	"*saveTimeout",		XrmoptionSepArg,	NULL},
  {"-move",	"*moveTimeout",		XrmoptionSepArg,	NULL},
  {"-blink",	"*blinkTimeout",	XrmoptionSepArg,	NULL},
  {"-reactivate","*reactivateProg",	XrmoptionSepArg,	NULL},
  {"-randomize","*randomize",		XrmoptionSepArg,	NULL},
  {"-detach",	"*detachInterval",	XrmoptionSepArg,	NULL},
  {"-idle",	"*reactivateTimeout",	XrmoptionSepArg,	NULL},
  {"-wait",	"*activateTimeout",	XrmoptionSepArg,	NULL},
  {"-restart",	"*restartTimeout",	XrmoptionSepArg,	NULL},
  {"-tty",	"*loginTty",		XrmoptionSepArg,	NULL},
  {"-session",	"*sessionScript",	XrmoptionSepArg,	NULL},
  {"-srvdcheck","*srvdCheck",		XrmoptionSepArg,	NULL},
  {"-fp",	"*fontPath",		XrmoptionSepArg,	NULL},
  {"-blankall", "*blankAll",		XrmoptionNoArg,   (caddr_t) "on"},
  {"-noblankall","*blankAll",		XrmoptionNoArg,   (caddr_t) "off"},
  {"-motdfile",	"*motdFile",		XrmoptionSepArg,	NULL},
  {"-motd2file","*motd2File",		XrmoptionSepArg,	NULL},
};

/* The structure containing the resource information for the
 * Xlogin application resources.
 */

#define Offset(field) (XtOffset(XLoginResources *, field))

static XtResource my_resources[] = {
  {"saveTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(save_timeout), XtRImmediate, (caddr_t) 120},
  {"moveTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(move_timeout), XtRImmediate, (caddr_t) 20},
  {"blinkTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(blink_timeout), XtRImmediate, (caddr_t) 40},
  {"reactivateProg", XtCFile, XtRString, sizeof(String),
     Offset(reactivate_prog), XtRImmediate, "/etc/athena/reactivate"},
  {"randomize", XtCInterval, XtRInt, sizeof(int),
     Offset(randomize), XtRImmediate, (caddr_t) 60},
  {"detachInterval", XtCInterval, XtRInt, sizeof(int),
     Offset(detach_interval), XtRImmediate, (caddr_t) 12},
  {"activateTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(activate_timeout), XtRImmediate, (caddr_t) 30},
  {"restartTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(restart_timeout), XtRImmediate, (caddr_t) (60 * 60 * 12)},
  {"reactivateTimeout", XtCInterval, XtRInt, sizeof(int),
     Offset(reactivate_timeout), XtRImmediate, (caddr_t) 300},
  {"loginTty", XtCFile, XtRString, sizeof(String),
     Offset(tty), XtRImmediate, (caddr_t) "ttyv0"},
  {"sessionScript", XtCFile, XtRString, sizeof(String),
     Offset(session), XtRImmediate, (caddr_t) "/etc/athena/login/Xsession"},
  {"srvdcheck", XtCFile, XtRString, sizeof(String),
     Offset(srvdcheck), XtRImmediate, (caddr_t) "/srvd/.rvdinfo"},
#ifdef SOLARIS
  {"fontPath", XtCString, XtRString, sizeof(String),
     Offset(fontpath), XtRImmediate, (caddr_t) "/usr/openwin/lib/fonts/" },
#else
  {"fontPath", XtCString, XtRString, sizeof(String),
     Offset(fontpath), XtRImmediate, (caddr_t) "/usr/athena/lib/X11/fonts/misc/,/usr/athena/lib/X11/fonts/75dpi/,/usr/athena/lib/X11/fonts/100dpi/" },
#endif
  {"loginName", XtCString, XtRString, sizeof(String),
     Offset(loginName), XtRImmediate, (caddr_t) "" },
  {"blankAllScreens", XtCBoolean, XtRBoolean, sizeof(Boolean),
     Offset(blankAll), XtRImmediate, (caddr_t) True},
  {"showMotd", XtCBoolean, XtRBoolean, sizeof(Boolean),
     Offset(showMotd), XtRImmediate, (caddr_t) True},
  {"motdFile", XtCString, XtRString, sizeof(String),
     Offset(motdFile), XtRImmediate, (caddr_t) MOTD_FILENAME },
  {"motd2File", XtCString, XtRString, sizeof(String),
     Offset(motd2File), XtRImmediate, (caddr_t) "" },
};

#undef Offset

XtActionsRec actions[] = {
    { "setfocus", focusACT },
    { "unsetfocus", unfocusACT },
    { "run", runACT },
    { "idleReset", idleReset },
    { "login", loginACT },
    { "reset", resetCB },
    { "setCorrectFocus", setcorrectfocus },
    { "signalConsoleACT", sigconsACT },
    { "callbackACT", callbackACT },
#ifdef sgi
    { "windowShutdownACT", windowShutdownACT },
#endif
};




#ifndef CONSOLEPID
#define CONSOLEPID "/var/athena/console.pid"
#endif

#ifndef UTMP_FILE
#ifdef _PATH_UTMP
#define UTMP_FILE _PATH_UTMP
#else /* _PATH_UTMP */
#define UTMP_FILE "/var/adm/utmp"
#endif /* _PATH_UTMP */
#endif /* UTMP_FILE */

/* Globals. */

XtIntervalId curr_timerid = 0, blink_timerid = 0, is_timerid = 0,
  react_timerid = 0;
Widget appShell;
Widget saver, ins;
Widget savershell[10];
int num_screens;
XLoginResources resources;
GC owlGC, isGC;
Display *dpy;
Window owlWindow, isWindow;
int owlNumBitmaps, isNumBitmaps;
/* unsigned */ int owlWidth, owlHeight, isWidth, isHeight;
int owlState, owlDelta, isDelta, owlTimeout, isTimeout;
Pixmap owlBitmaps[20], isBitmaps[20];
struct timeval starttime;
pid_t activation_pid, attach_pid, attachhelp_pid, quota_pid;
int activation_state, activate_count = 0, attach_state, attachhelp_state;
int exiting = FALSE;
extern char *defaultpath;
char login[128], passwd[128];
sigset_t sig_zero;

#ifdef SOLARIS_MAE
int netspy = FALSE;
#endif

/* Local Globals */

static struct sigaction sigact, osigact;

void main(argc, argv)
     int argc;
     char* argv[];
{
  XtAppContext app;
  Widget hitanykey, namew;
  Display *dpy1;
  char hname[1024], *c;
  Arg args[1];
  int i;
  unsigned acc = 0;
  int pid;

  sigemptyset(&sig_zero);

#ifdef sgi
  /* Get stderr and stdout for our own uses - we don't want them going
   * through various paths of xdm. Under Irix, xdm does a lot of the
   * actually logging-in; it calls xlogin with stdout a pipe it listens
   * to to determine whom to log in. We need this communication, but we
   * also want stdout to work correctly (out to console). So we make
   * a copy of the stdout stream, and then reopen stdout to whatever
   * tty we belong to (or /dev/console, if that doesn't work).
   */
  if (nanny_getTty(athconsole, sizeof(athconsole)))
    strcpy(athconsole, "/dev/console");

  xdmfd = dup(fileno(stdout));
  if (xdmfd != -1)
    {
      xdmstream = fdopen(xdmfd, "w");
      if (NULL == freopen(athconsole, "w", stdout))
	(void)freopen("/dev/console", "w", stdout);
    }
  else
    xdmstream = stdout; /* Some stuff will break, but better than losing. */
  /* Actually, losing gracefully might be wise... */

  if (NULL == freopen(athconsole, "w", stderr))
    (void)freopen("/dev/console", "w", stderr);
  /* if (stderr == NULL)
     tough luck; */
#endif

  /* Have to find this argument before initializing the toolkit.
   * We set both XUSERFILESEARCHPATH and XENVIRONMENT.  The effect is
   * that the -config argument names a directory that will have the
   * file Xlogin which contains the resources, and may optionally have
   * a Xlogin.local file containing additional resources which will
   * override those in the regular file.
   */
  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-config") && (i + 1 < argc))
      {
	c = getenv("XUSERFILESEARCHPATH");
	if (c)
	  sprintf(hname, "%s:%s/%%N", c, argv[i + 1]);
	else
	  sprintf(hname, "%s/%%N", argv[i + 1]);
	psetenv("XUSERFILESEARCHPATH", hname, 1);
	sprintf(hname, "%s/Xlogin.local", argv[i + 1]);
	psetenv("XENVIRONMENT", hname, 1);
	break;
      }

  /* Initialize Toolkit creating the application shell, and get
   * application resources.
   */
  appShell = XtInitialize("xlogin", "Xlogin",
			  options, XtNumber(options),
			  &argc, argv);
  add_converter();
  app = XtWidgetToApplicationContext(appShell);
  XtAppSetErrorHandler(app, localErrorHandler);
  dpy = XtDisplay(appShell);
  XtAppAddActions(app, actions, XtNumber(actions));

  XtGetApplicationResources(appShell, (caddr_t)&resources, 
			    my_resources, XtNumber(my_resources),
			    NULL, (Cardinal)0);

#ifndef sgi
  /* Tell the display manager we're ready, just like the X server
   * handshake. This code used to be right before XtMainLoop. However,
   * under Ultrix dm is required to open /dev/xcons and manually pipe
   * it to the console window. It won't start this process until
   * it gets its SIGUSR1 from us. So, if we do output to the console
   * (where our stderr and stdout are directed) before sending the SIGUSR1,
   * it may show up as "black bar" messages. This is suboptimal. Since
   * I have no idea why this handshake is helpful in the first place,
   * beyond knowing the exec of XLogin succeeded, I don't see any reason
   * not to just get it over with and get the console flowing when we
   * need it. We need it now. --- cfields
   */
  sigaction(SIGUSR1, NULL, &osigact);
  if (osigact.sa_handler == SIG_IGN)
    kill(getppid(), SIGUSR1);
#endif /* not sgi */

#ifdef SOLARIS_MAE
  /* Make sure the network device has the proper owner and protections.
   * But don't muck with it unless the file NETSPY exists. */

  netspy = file_exists(NETSPY);
  if (netspy)
    {
      chown(NETDEV, ROOT, SYS);
      chmod(NETDEV, 0600);
    }
#endif

  /* Call reactivate with the -prelogin option. This restores /etc/passwd,
   * blows away stray processes, runs access_off, and a couple of other
   * low overhead things (if PUBLIC=true). This is low overhead because
   * we want login to start up ASAP, but we pay the price for what we do
   * to make sure the workstation is as clean as it ought to be with respect
   * to performance and security. This code has to come after the resources
   * are loaded, so we know where the reactivate script is.
   */
  pid = fork();
  switch(pid)
    {
    case 0:
      execl(resources.reactivate_prog, resources.reactivate_prog,
	    "-prelogin", 0);
      fprintf(stderr, "XLogin: unable to exec reactivate program \"%s\"\n",
	      resources.reactivate_prog);
      _exit(1);
      break;
    case -1:
      fprintf(stderr, "XLogin: unable to fork for reactivatation\n");
      break;
    default:
      waitpid(pid, NULL, 0);
      break;
    }

  /* We set up the signal handler later than we used to because we don't
   * need or want it to be running to handle the prelogin script.
   */
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  sigact.sa_handler = catch_child;
  sigaction(SIGCHLD, &sigact, NULL);

  WcRegisterCallback(app, "UnsetFocus", unfocusACT, NULL);
  WcRegisterCallback(app, "runCB", runCB, NULL);
  WcRegisterCallback(app, "setfocusCB", focusCB, NULL);
  WcRegisterCallback(app, "resetCB", resetCB, NULL);
  WcRegisterCallback(app, "signalConsoleCB", sigconsCB, NULL);
  WcRegisterCallback(app, "idleResetCB", idleReset, NULL);
  WcRegisterCallback(app, "attachAndRunCB", attachandrunCB, NULL);
#ifdef sgi
  WcRegisterCallback(app, "windowShutdownCB", windowShutdownCB, NULL);
#endif

  /* Register all Athena widget classes. */
  AriRegisterAthena (app);
  WcRegisterClassPtr(app, "ClockWidget", clockWidgetClass);
  WcRegisterClassPtr(app, "clockWidgetClass", clockWidgetClass);

  /* Clear console window. */
  sigconsCB(NULL, "clear", NULL);

  /* Create widget tree below toplevel shell using Xrm database. */
  WcWidgetCreation(appShell);

  /* Realize the widget tree, finish up initializing, and enter the
   * main application loop.
   */
  XtRealizeWidget(appShell);

  initOwl(appShell);		/* widget tree MUST be realized... */
  adjustOwl(appShell);

  /* Put the first component of the hostname in the label of the host
   * widget. */
  gethostname(hname, sizeof(hname));
  c = strchr(hname, '.');
  if (c)
      *c = 0;
  XtSetArg(args[0], XtNlabel, hname);
  namew = WcFullNameToWidget(appShell, "*login*host");
  XtSetValues(namew, args, 1);
  namew = WcFullNameToWidget(appShell, "*instructions*host");
  XtSetValues(namew, args, 1);
  XtSetArg(args[0], XtNstring, login);
  XtSetValues(WcFullNameToWidget(appShell, "*name_input"), args, 1);
  XtSetArg(args[0], XtNstring, passwd);
  XtSetValues(WcFullNameToWidget(appShell, "*pword_input"), args, 1);

  /* Seed the random number generator with our hostname. */
  c = hname;
  while (*c)
    acc = (acc << 1) ^ *c++;
  srandom(acc);
  resources.reactivate_timeout += random() % resources.randomize;
  saver = WcFullNameToWidget(appShell, "*savershell");
  ins = WcFullNameToWidget(appShell, "*instructions");
  hitanykey = WcFullNameToWidget(appShell, "*hitanykey");

#define MASK	KeyReleaseMask | ButtonReleaseMask
  XtAddEventHandler(saver, MASK, FALSE, unsave, (XtPointer)TRUE);
  XtAddEventHandler(hitanykey, MASK, FALSE, unsave, (XtPointer)TRUE);

  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000, screensave, NULL);
  blink_timerid = XtAddTimeOut(1000, blinkOwl, NULL);
  is_timerid = XtAddTimeOut(1000, blinkIs, NULL);
  gettimeofday(&starttime, NULL);
  resetCB(namew, NULL, NULL);
  if (access(resources.srvdcheck, F_OK) != 0)
    start_reactivate(NULL, NULL);
  else
    activation_state = ACTIVATED;

  psetenv("PATH", defaultpath, 1);
#ifdef HOSTTYPE
  psetenv("hosttype", HOSTTYPE, 1); /* environment.h */
#endif

  /* Create shells to blank out all other screens, if any... */

  /* Cover ourselves by setting number of screens to one. */
  num_screens = 1;
  savershell[0] = saver;	/* Fill in the saver shell for now... */

  if (resources.blankAll && ScreenCount(dpy) > 1)
    {
      int this_screen = XScreenNumberOfScreen(XtScreen(saver));
      char *orig_dpy, *ptr;
      int zero = 0;
      num_screens = MIN(ScreenCount(dpy), 10);
      orig_dpy = XtNewString(DisplayString(dpy));

      /* If the alloc worked, continue. We are counting on
       * displaystring to always contain a period. The displaystring
       * is always canonicalized for us...  isn't that nice of them?
       */
      if (orig_dpy != NULL)	
	{
	  if ((ptr = strrchr(orig_dpy, '.')) != NULL)
	    {
	      ptr++;
	      /* Only does screens 0 thru 9. If you have more screens
	       * than that, you lose.
	       */
	      for (i = 0; i < ScreenCount(dpy) && i < 10; i++)
		{
		  if (i == this_screen)
		    savershell[i] = saver;
		  else
		    {
		      Widget root;

		      *ptr = (char) i + '0';
		      dpy1 = XtOpenDisplay(app, orig_dpy, "xlogin", "Xlogin",
					   NULL, 0, &zero, NULL);
		      root = XtAppCreateShell("xlogin", "Xlogin",
					      applicationShellWidgetClass,
					      dpy1, NULL, 0);
		      savershell[i] = XtCreatePopupShell("savershell",
						 transientShellWidgetClass,
						 root, NULL, 0);
		      XtAddEventHandler(savershell[i], MASK,
					FALSE, unsave, (XtPointer)TRUE);
		    }
		}
	    }
	}
      XtFree(orig_dpy);
    }

  XtMainLoop();
}

static Dimension x_max = 0, y_max = 0;

static void move_instructions(data, timerid)
     XtPointer data;
     XtIntervalId *timerid;
{
  Position x, y;
  Window wins[2];

  if (!x_max)		/* Get sizes if we haven't done so already. */
    {
      Arg args[2];

      XtSetArg(args[0], XtNwidth, &x_max);
      XtSetArg(args[1], XtNheight, &y_max);
      XtGetValues(ins, args, 2);

      if (WidthOfScreen(XtScreen(ins)) < x_max + 1)
	x_max = 1;
      else
	x_max = WidthOfScreen(XtScreen(ins)) - x_max;

      if (HeightOfScreen(XtScreen(ins)) < y_max + 1)
	y_max = 1;
      else
	y_max = HeightOfScreen(XtScreen(ins)) - y_max;
    }

  x = random() % x_max;
  y = random() % y_max;
  XtMoveWidget(ins, x, y);

  if (activation_state != REACTIVATING)
    {
      XRaiseWindow(XtDisplay(ins), XtWindow(ins));
      wins[0] = XtWindow(ins);
      wins[1] = XtWindow(saver);
      XRestackWindows(XtDisplay(ins), wins, 2);
    }

  curr_timerid = XtAddTimeOut(resources.move_timeout * 1000,
			      move_instructions, NULL);
}

static void start_reactivate(data, timerid)
     XtPointer  data;
     XtIntervalId  *timerid;
{
  int in_use = 0;
  int file;
  struct utmp utmp;
  struct timeval now;

#ifndef sgi /* Not our problem on the SGI. */
  gettimeofday(&now, NULL);
  if (now.tv_sec - starttime.tv_sec > resources.restart_timeout)
    {
      fprintf(stderr, "Restarting X Server\n");
      exit(0);
    }
#endif /* sgi */

  do_motd();

  file = open(UTMP_FILE, O_RDONLY, 0);
  if (file >= 0)
    {
      while (read(file, (char *) &utmp, sizeof(utmp)) > 0)
	{
	  if (utmp.ut_name[0] != 0
#ifdef SYSV
	      && utmp.ut_type == USER_PROCESS
#endif
	      )
	    {
	      in_use = 1;
	      break;
	    }
	}
      close(file);
    }

  if (in_use || activation_state == REACTIVATING)
    {
      react_timerid = XtAddTimeOut(resources.reactivate_timeout * 1000,
				   start_reactivate, NULL);
      return;
    }

  /* Clear the console window. */
  sigconsCB(NULL, "clear", NULL);

  activation_state = REACTIVATING;
  switch(fork_and_store(&activation_pid))
    {
    case 0:
      if (activate_count % resources.detach_interval == 0)
	execl(resources.reactivate_prog, resources.reactivate_prog,
	      "-detach", 0);
      else
	execl(resources.reactivate_prog, resources.reactivate_prog, 0);
      fprintf(stderr, "XLogin: unable to exec reactivate program \"%s\"\n",
	      resources.reactivate_prog);
      _exit(1);
    case -1:
      fprintf(stderr, "XLogin: unable to fork for reactivatation\n");
      activation_state = ACTIVATED;
      break;
    default:
      break;
    }
  activate_count++;
  react_timerid = XtAddTimeOut(resources.reactivate_timeout * 1000,
			       start_reactivate, NULL);
}

void idleReset()
{
  if (curr_timerid)
    XtRemoveTimeOut(curr_timerid);
  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000,
			      screensave, NULL);
}

static void stop_activate(data, timerid)
     XtPointer data;
     XtIntervalId *timerid;
{
  if (activation_state == ACTIVATED)
    return;

  kill(activation_pid, SIGKILL);
  fprintf(stderr, "Workstation activation failed to finish normally.\n");
  activation_state = ACTIVATED;
}

static void screensave(data, timerid)
     XtPointer  data;
     XtIntervalId  *timerid;
{
  static int first_time = TRUE;
  Pixmap pixmap;
  Cursor cursor;
  XColor c;
  int i;

  XtPopdown(WcFullNameToWidget(appShell, "*getSessionShell"));
  XtPopdown(WcFullNameToWidget(appShell, "*warningShell"));
  XtPopdown(WcFullNameToWidget(appShell, "*queryShell"));

  for (i = 0; i < num_screens; i++)
    XtPopup(savershell[i], XtGrabNone);

  do_motd();
  XtPopup(ins, XtGrabNone);
  XRaiseWindow(XtDisplay(ins), XtWindow(ins));
  unfocusACT(appShell, NULL, NULL, NULL);
  if (first_time)
    {
      /* Contortions to "get rid of" cursor on screensaver windows. */
      c.pixel = BlackPixel(dpy, DefaultScreen(dpy));
      XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);

      pixmap = XCreateBitmapFromData(dpy, XtWindow(appShell), "", 1, 1);

      cursor = XCreatePixmapCursor(dpy, pixmap, pixmap, &c, &c,
				   (unsigned int)0, (unsigned int)0);
      XFreePixmap(dpy, pixmap);
      for (i = 0; i < num_screens; i++)
	XDefineCursor(dpy, XtWindow(savershell[i]), cursor);
      XDefineCursor(dpy, XtWindow(ins), cursor);
      XFreeCursor(dpy, cursor);

      first_time = FALSE;
    }

  if (blink_timerid != 0)	/* Don't do animation while screensaved. */
    XtRemoveTimeOut(blink_timerid);
  blink_timerid = 0;
  if (is_timerid != 0)
    XtRemoveTimeOut(is_timerid);
  is_timerid = 0;

  /* Don't let the X server's screensaver kick in. */
  XSetScreenSaver(dpy, 0, -1, DefaultBlanking, DefaultExposures);
  curr_timerid = XtAddTimeOut(resources.move_timeout * 1000,
			      move_instructions, NULL);
  react_timerid = XtAddTimeOut(resources.reactivate_timeout * 1000,
			       start_reactivate, NULL);
}

/* Check the motd file and update the contents of the widget if necessary. */
do_motd()
{
  static Widget motdtext = NULL;
  static time_t modtime = 0, modtime2 = 0;
  struct stat stbuf, stbuf2;
  Arg args[1];
  char buf[10000], *temp, *s, *d;
  int fid, len, do_g_motd, do_l_motd;

  if (!motdtext)
    {
      motdtext = WcFullNameToWidget(appShell, "*motd");

      /* Initialize motdtext to NULL in case it never gets set.
       * This happens in the case of a bad stat on the motd
       * file, or if showMotd false.
       */
      buf[0] = '\0';
      XtSetArg(args[0], XtNlabel, buf);
      XtSetValues(motdtext, args, 1);
    }

  if (resources.showMotd)
    {
      do_g_motd = (resources.motdFile != NULL && *resources.motdFile &&
		   !stat(resources.motdFile, &stbuf) && 
		   stbuf.st_mtime != modtime);
      if (do_g_motd)
	modtime = stbuf.st_mtime;

      do_l_motd = (resources.motd2File != NULL && *resources.motd2File &&
		   !stat(resources.motd2File, &stbuf2) &&
		   stbuf2.st_mtime != modtime2);
      if (do_l_motd)
	modtime2 = stbuf2.st_mtime;

      if (do_g_motd || do_l_motd)
	{
	  /* Read the new motd. */
	  len = 0;
	  if (resources.motdFile != NULL && *resources.motdFile &&
	      (fid = open(resources.motdFile, O_RDONLY)) >= 0)
	    {
	      len = read(fid, buf, sizeof(buf));
	      close(fid);
	    }
	  if (resources.motd2File != NULL && *resources.motd2File &&
	      (fid = open(resources.motd2File, O_RDONLY)) >= 0)
	    {
	      len += read(fid, &(buf[len]), sizeof(buf) - len);
	      close(fid);
	    }
	  buf[len] = 0;

	  /* De-tabbify the motd (label widgets don't do tabs). */
	  for (s = buf; *s; s++)
	    if (*s == '\t') len += 7;
	  d = temp = malloc(len + 1);
	  len = 0;
	  for (s = buf; *s; s++)
	    {
	      switch(*s)
		{
		case '\t':
		  *d++ = ' ';
		  len++;
		  while (len++ % 8 != 0)
		    *d++ = ' ';
		  len--;
		  break;
		case '\n':
		  len = 0;
		  *d++ = *s;
		  break;
		default:
		  *d++ = *s;
		  len++;
		}
	    }
	  *d = 0;

	  /* Now set the text. */
	  XtSetArg(args[0], XtNlabel, temp);
	  XtSetValues(motdtext, args, 1);
	  free(temp);

	  /* Force move_instructions() to recompute size. */
	  x_max = 0;
	}
    }
}

static void unsave(w, popdown, event, bool)
     Widget w;
     int popdown;
     XEvent *event;
     Boolean *bool;
{
  /* Hide the console window. */
  sigconsCB(NULL, "hide", NULL);

  if (popdown)
    {
      int i;

      XtPopdown(ins);
      for (i = 0; i < num_screens; i++)
	XtPopdown(savershell[i]);
    }

  /* Enable the X server's screensaver. */
  XSetScreenSaver(dpy, -1, -1, DefaultBlanking, DefaultExposures);
  resetCB(w, NULL, NULL);

  if (curr_timerid != 0)
    XtRemoveTimeOut(curr_timerid);
  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000,
			      screensave, NULL);
  blink_timerid = XtAddTimeOut(random() % (10 * 1000),
			       blinkOwl, NULL);
  is_timerid = XtAddTimeOut(random() % (10 * 1000),
			    blinkIs, NULL);
  if (react_timerid != 0)
    XtRemoveTimeOut(react_timerid);
  if (activation_state == REACTIVATING)
    react_timerid = XtAddTimeOut(resources.activate_timeout * 1000,
				 stop_activate, NULL);
}

void loginACT(w, event, p, n)
     Widget w;
     XEvent *event;
     String *p;
     Cardinal *n;
{
  Arg args[2];
  char *script;
  int mode = 1;
  Pixmap bm1, bm2, bm3, bm4, bm5;
  XawTextBlock tb;
  extern char *dologin();
  XEvent e;

  if (curr_timerid)
    XtRemoveTimeOut(curr_timerid);

  XtSetArg(args[0], XtNleftBitmap, &bm1);
  XtGetValues(WcFullNameToWidget(appShell, "*lmenuEntry1"), args, 1);
  XtSetArg(args[0], XtNleftBitmap, &bm2);
  XtGetValues(WcFullNameToWidget(appShell, "*lmenuEntry2"), args, 1);
  XtSetArg(args[0], XtNleftBitmap, &bm3);
  XtGetValues(WcFullNameToWidget(appShell, "*lmenuEntry3"), args, 1);
  XtSetArg(args[0], XtNleftBitmap, &bm4);
  XtGetValues(WcFullNameToWidget(appShell, "*lmenuEntry4"), args, 1);
  XtSetArg(args[0], XtNleftBitmap, &bm5);
  XtGetValues(WcFullNameToWidget(appShell, "*lmenuEntry5"), args, 1);

  /* Determine which option was selected by seeing which 4 of the 5 match. */
  if (bm1 == bm2 && bm1 == bm3 && bm1 == bm4)
    mode = 5;
  if (bm1 == bm2 && bm1 == bm3 && bm1 == bm5)
    mode = 4;
  if (bm1 == bm2 && bm1 == bm4 && bm1 == bm5)
    mode = 3;
  if (bm1 == bm3 && bm1 == bm4 && bm1 == bm5)
    mode = 2;
  if (bm2 == bm3 && bm2 == bm4 && bm2 == bm5)
    mode = 1;

  XtSetArg(args[0], XtNstring, &script);
  XtGetValues(WcFullNameToWidget(appShell, "*getsession*value"), args, 1);
  unfocusACT(appShell, NULL, NULL, NULL);
  XtUnmapWidget(appShell);
  /* To clear the cut buffer in case someone types ^U while typing
   * their password.
   */
  XDeleteProperty(dpy, DefaultRootWindow(dpy), XA_CUT_BUFFER0);
  XDeleteProperty(dpy, DefaultRootWindow(dpy), XA_CUT_BUFFER1);
  XFlush(dpy);

  /* Wait for activation to finish.  We play games with signals here
   * because we are not waiting within the XtMainloop for it to handle
   * the timers.
   */
  if (activation_state != ACTIVATED)
    {
      void (*oldsig)();

      fprintf(stderr, "Waiting for workstation to finish activating...");
      fflush(stderr);
      sigemptyset(&sigact.sa_mask);
      sigact.sa_flags = 0;
      sigact.sa_handler = stop_activate;
      sigaction(SIGALRM, &sigact, &osigact);
      alarm(resources.activate_timeout); 
      while (activation_state != ACTIVATED)
	sigsuspend(&sig_zero);
      alarm(0);
      sigaction(SIGALRM, &osigact, NULL);
      fprintf(stderr, "done.\n");
    }

  if (access(resources.srvdcheck, F_OK) != 0)
    tb.ptr = "Workstation failed to activate successfully.  "
      "Please notify the Athena Hotline, x3-1410, hotline@mit.edu.";
  else
    {
      setFontPath();
#ifdef sgi
      /* We obtained the tty earlier from nanny. */
      resources.tty = athconsole + 5;
#endif

      XWarpPointer(dpy, None, RootWindow(dpy, DefaultScreen(dpy)),
		   0, 0, 0, 0, 300, 300);
      XFlush(dpy);
      tb.ptr = dologin(login, passwd, mode, script, resources.tty,
		       resources.session, DisplayString(dpy));
      XWarpPointer(dpy, None, RootWindow(dpy, DefaultScreen(dpy)),
		   0, 0, 0, 0, WidthOfScreen(DefaultScreenOfDisplay(dpy))/2,
		   HeightOfScreen(DefaultScreenOfDisplay(dpy))/2);
    }

  XtMapWidget(appShell);
  XtPopup(WcFullNameToWidget(appShell, "*warningShell"), XtGrabExclusive);
  tb.firstPos = 0;
  tb.length = strlen(tb.ptr);
  tb.format = FMT8BIT;
  XawTextReplace(WcFullNameToWidget(appShell, "*warning*value"),
		 0, 65536, &tb);
  XtCallActionProc(WcFullNameToWidget(appShell, "*warning*value"),
		   "form-paragraph", &e, NULL, 0);
  focusCB(appShell, "*warning*value", NULL);
  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000,
			      screensave, NULL);
}

/* Login failed: Set the exit flag, then return the message the usual way. */
char *lose(msg)
     char *msg;
{
  exiting = TRUE;
  return msg;
}

void focusACT(w, event, p, n)
     Widget w;
     XEvent *event;
     String *p;
     Cardinal *n;
{
  Widget target;

#ifdef sgi
  static int done_once = 0;

  /* This crock works around the an invalid argument error on the
   * XSetInputFocus() call below the very first time it is called,
   * only when running on the RIOS.  We still don't know just what
   * causes it.
   * I sure wish I'd modified this comment when I added sgi to
   * the ifdef the first time. I should try taking it back out to
   * find out.
   */
  if (done_once == 0)
    {
      done_once++;
      XSync(dpy, FALSE);
      sleep(1);
      XSync(dpy, FALSE);
    }
#endif

  target = WcFullNameToWidget(appShell, p[0]);
  XSetInputFocus(dpy, XtWindow(target), RevertToPointerRoot, CurrentTime);

  if (owlState == OWL_SLEEPY)
    if (blink_timerid != 0)
      {
	XtRemoveTimeOut(blink_timerid);
	blink_timerid = 0;
	owlDelta = OWL_WAKING;
	blinkOwl(NULL, NULL);
      }
}

void focusCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  Cardinal one = 1;

  focusACT(w, NULL, &s, &one);
}

void unfocusACT(w, event, p, n)
     Widget w;
     XEvent *event;
     String *p;
     Cardinal *n;
{
  int rvt;
  Window win;

  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XGetInputFocus(dpy, &win, &rvt);
}

void setcorrectfocus(w, event, p, n)
     Widget w;
     XEvent *event;
     String *p;
     Cardinal *n;
{
  Arg args[2];
  char *win;
  Cardinal i;
  Boolean bool;

  XtSetArg(args[0], XtNdisplayCaret, &bool);
  XtGetValues(WcFullNameToWidget(appShell, "*name_input"), args, 1);
  if (bool)
    win = "*name_input";
  else
    win = "*pword_input";
  i = 1;
  focusACT(w, event, &win, &i);
}

void runACT(w, event, p, n)
     Widget w;
     XEvent *event;
     char **p;
     Cardinal *n;
{
  char **argv;
  int i;
#ifdef sgi
  extern char **environ;
#endif

  unfocusACT(w, event, p, n);
  argv = (char **)malloc(sizeof(char *) * (*n + 3));
  argv[0] = "sh";
  argv[1] = "-c";
  for (i = 0; i < *n; i++)
    argv[i+2] = p[i];
  argv[i+2] = NULL;

  /* Wait for activation to finish. */
  if (activation_state != ACTIVATED)
    fprintf(stderr, "Waiting for workstation to finish activating...\n");
  while (activation_state != ACTIVATED)
    sigsuspend(&sig_zero);
  if (access(resources.srvdcheck, F_OK) != 0)
    {
      fprintf(stderr, "Workstation failed to activate successfully.\n"
	      "Please notify the Athena Hotline, x3-1410, hotline@mit.edu.");
      return;
    }
  sigconsCB(NULL, "hide", NULL);
  setFontPath();
  XFlush(dpy);
  XtCloseDisplay(dpy);

  /* Set up the pre-login environment.
   *
   * By default, all of xlogin's environment is passed to the
   * pre-login applications. Some of xlogin's environment is
   * not appropriately passed on; if such a new element is
   * introduced into xlogin, it should be unsetenved here.
   *
   * Note that the environment for user logins is set
   * up in verify.c: it is NOT RELATED to this environment
   * setup. If you add a new environment variable here,
   * consider whether or not it also needs to be added there.
   * Note that variables that need to be unsetenved here do not
   * need similar treatment in the user login area, since there
   * no variables are passed by default.
   *
   * Note also that below are not the only environment variables
   * mucked with. Others are done earlier for other functions
   * of xlogin.
   */
  punsetenv("XUSERFILESEARCHPATH");
  punsetenv("XENVIRONMENT");

  psetenv("PATH", defaultpath, 1);
  psetenv("USER", "daemon", 1);
  psetenv("SHELL", "/bin/sh", 1);
  psetenv("DISPLAY", ":0", 1);

#ifdef sgi
  psetenv("PRELOGIN", "true", 1);
  if (nanny_setupUser(N_DAEMON, 0, environ, argv))
    {
      fprintf(stderr, "Unable set up for daemon app\n");
      return;
    }

  fprintf(xdmstream, "%s", N_DAEMON);
  fputc(0, xdmstream);

  exit(0);
#else
  setgroups(sizeof(def_grplist)/sizeof(gid_t), def_grplist);

  setgid(def_grplist[0]);
  if (setuid(DAEMON))
    {
      fprintf(stderr, "Unable to set user id.\n");
      return;
    }
  execv("/bin/sh", argv);
  fprintf(stderr, "XLogin: unable to exec /bin/sh\n");
  _exit(3);
#endif
}

void runCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  Cardinal i = 1;

  runACT(w, NULL, &s, &i);
}

void attachandrunCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  char *cmd, locker[256];
  Cardinal i = 1;

  cmd = strchr(s, ',');
  if (cmd == NULL)
    {
      fprintf(stderr,
	      "Xlogin warning: need two arguments in AttachAndRun(%s)\n",
	      s);
      return;
    }
  strncpy(locker, s, cmd - s);
  locker[cmd - s] = 0;
  cmd++;

  attach_state = -1;
  switch(fork_and_store(&attach_pid))
    {
    case 0:
      execlp("attach", "attach", "-n", "-h", "-q", locker, NULL);
      fprintf(stderr, "Xlogin warning: unable to attach locker %s\n", locker);
      _exit(1);
    case -1:
      fprintf(stderr, "Xlogin: unable to fork to attach locker\n");
      break;
    default:
      while (attach_state == -1)
	sigsuspend(&sig_zero);
      if (attach_state != 0)
	{
	  fprintf(stderr, "Unable to attach locker %s, aborting...\n",
		  locker);
	  return;
	}
    }

  runACT(w, NULL, &cmd, &i);
}

#ifdef sgi
void windowShutdownCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  /* If this returns 0, the X server has been killed and it's time
   * to go. If not, we should probably pop up a dialog box.
   */
  if (!nanny_setConsoleMode())
    exit(0);
}

void windowShutdownACT(w, event, p, n)
     Widget w;
     XEvent *event;
     char **p;
     Cardinal *n;
{
  if (!nanny_setConsoleMode())
    exit(0);
}
#endif

void sigconsACT(w, event, p, n)
     Widget w;
     XEvent *event;
     char **p;
     Cardinal *n;
{
  int sig, pid;
  FILE *f;
  char buf[BUFSIZ];

  if (!strcmp(p[0], "clear"))
    sig = SIGFPE;
  else if (!strcmp(p[0], "hide"))
    sig = SIGUSR2;
  else if (!strcmp(p[0], "show"))
    sig = SIGUSR1;
  else if (!strcmp(p[0], "config"))
    sig = SIGHUP;
  else
    sig = atoi(p[0]);

  f = fopen(CONSOLEPID, "r");
  if (f)
    {
      fgets(buf, sizeof(buf), f);
      pid = atoi(buf);
      if (pid)
	kill(pid, sig);
      fclose(f);
    }
}

void sigconsCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  Cardinal i = 1;

  sigconsACT(w, NULL, &s, &i);
}

void callbackACT(w, event, p, n)
     Widget w;
     XEvent *event;
     char **p;
     Cardinal *n;
{
  w = WcFullNameToWidget(appShell, p[0]);
  XtCallCallbacks(w, "callback", p[1]);
}

void resetCB(w, s, unused)
     Widget w;
     char *s;
     caddr_t unused;
{
  XawTextBlock tb;
  Widget name_input;

  if (exiting == TRUE)
    exit(0);
  focusCB(appShell, "*name_input", NULL);
  WcSetValueCB(appShell, "*lmenuEntry1.leftBitmap: check", NULL);
  WcSetValueCB(appShell, "*lmenuEntry2.leftBitmap: white", NULL);
  WcSetValueCB(appShell, "*lmenuEntry3.leftBitmap: white", NULL);
  WcSetValueCB(appShell, "*lmenuEntry4.leftBitmap: white", NULL);
  WcSetValueCB(appShell, "*lmenuEntry5.leftBitmap: white", NULL);
  WcSetValueCB(appShell, "*selection.label:  ", NULL);
  WcSetValueCB(appShell, "*name_input.displayCaret: TRUE", NULL);

  tb.firstPos = tb.length = 0;
  tb.ptr = "";
  tb.format = FMT8BIT;
  XawTextReplace(WcFullNameToWidget(appShell, "*pword_input"), 0, 65536, &tb);
  XawTextReplace(WcFullNameToWidget(appShell, "*getsession*value"),
		 0, 65536, &tb);

  if (resources.loginName != NULL  &&  strlen(resources.loginName) != 0)
    {
      if (strlen(resources.loginName) > 8)
	resources.loginName[8] = '\0';
      tb.ptr = resources.loginName;
      tb.length = strlen(tb.ptr);
    }
  name_input = WcFullNameToWidget(appShell, "*name_input");
  XawTextReplace(name_input, 0, 65536, &tb);
  XawTextSetInsertionPoint(name_input, (XawTextPosition) tb.length);

  if (curr_timerid)
    XtRemoveTimeOut(curr_timerid);
  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000,
			      screensave, NULL);
}

setvalue(w, done, unused)
     Widget w;
     int *done;
{
  *done = 1;
}

typedef struct _prompt_callback_info {
  void (*abort_proc)();
  void *abort_arg;
} prompt_callback_info;

void click_abort(widget, client_data, call_data)
     Widget widget;
     XtPointer client_data, call_data;
{
  prompt_callback_info *cdata = (prompt_callback_info *)client_data;

  cdata->abort_proc(cdata->abort_arg);
}

void timeout_abort(client_data, timer_id)
     XtPointer client_data;
     XtIntervalId *timer_id;
{
  prompt_callback_info *cdata = (prompt_callback_info *)client_data;

  cdata->abort_proc(cdata->abort_arg);
}

prompt_user(msg, abort_proc, abort_arg)
     char *msg;
     void (*abort_proc)();
     void *abort_arg;
{
  XawTextBlock tb;
  XEvent e;
  prompt_callback_info cb;
  static void (*oldcallback)() = NULL;
  static int done;

  cb.abort_proc = abort_proc;
  cb.abort_arg = abort_arg;

  XtPopup(WcFullNameToWidget(appShell, "*queryShell"), XtGrabExclusive);
  tb.firstPos = 0;
  tb.ptr = msg;
  tb.length = strlen(msg);
  tb.format = FMT8BIT;
  XawTextReplace(WcFullNameToWidget(appShell, "*query*value"),
		 0, 65536, &tb);
  XtCallActionProc(WcFullNameToWidget(appShell, "*query*value"),
		   "form-paragraph", &e, NULL, 0);
  focusCB(appShell, "*query*value", NULL);
  if (oldcallback)
    XtRemoveCallback(WcFullNameToWidget(appShell, "*query*giveup"),
		     XtNcallback, oldcallback, NULL);
  else
    XtAddCallback(WcFullNameToWidget(appShell, "*query*cont"),
		  XtNcallback, (XtCallbackProc)setvalue, &done);
  XtAddCallback(WcFullNameToWidget(appShell, "*query*giveup"),
		XtNcallback, click_abort, &cb);
  oldcallback = abort_proc;
  curr_timerid = XtAddTimeOut(resources.save_timeout * 1000,
			      timeout_abort, &cb);

  /* Repeat main_loop here so we can check status & return. */
  done = 0;
  while (!done)
    {
      XtAppNextEvent((XtAppContext)_XtDefaultAppContext(), &e);
      XtDispatchEvent(&e);
    }

  XtRemoveTimeOut(curr_timerid);
  curr_timerid = 0;
  XtPopdown(WcFullNameToWidget(appShell, "*queryShell"));
  XFlush(dpy);
}

#define updateOwl()	XCopyPlane(dpy, owlBitmaps[owlCurBitmap], \
				   owlWindow, owlGC, 0, 0, \
				   owlWidth, owlHeight, 0, 0, 1)
#define updateIs()	XCopyPlane(dpy, isBitmaps[isCurBitmap], \
				   isWindow, isGC, 0, 0, \
				   isWidth, isHeight, 0, 0, 1)

static void blinkOwl(data, intervalid)
     XtPointer data;
     XtIntervalId *intervalid;
{
  static int owlCurBitmap;
  owlTimeout = 0;

  if (owlNumBitmaps == 0)
    return;

  switch(owlDelta)
    {
    case OWL_BLINKINGCLOSED:	/* your eyelids are getting heavy... */
      owlCurBitmap++;
      updateOwl();
      if (owlCurBitmap == owlNumBitmaps - 1)
	owlDelta = OWL_BLINKINGOPEN;
      break;

    case OWL_BLINKINGOPEN:	/* you will awake, feeling refreshed... */
      owlCurBitmap--;
      updateOwl();
      if (owlCurBitmap == ((owlState == OWL_SLEEPY) * (owlNumBitmaps) / 2))
	{
	  owlTimeout = random() % (10 * 1000);
	  owlDelta = OWL_BLINKINGCLOSED;
	}
      break;

    case OWL_SLEEPING:		/* transition to sleeping state */
      owlCurBitmap++;
      updateOwl();
      if (owlCurBitmap == ((owlState == OWL_SLEEPY) * (owlNumBitmaps) / 2))
	{
	  owlDelta = OWL_BLINKINGCLOSED;
	  owlTimeout = random() % (10 * 1000);
	}
      break;

    case OWL_WAKING:		/* transition to waking state */
      if (owlCurBitmap)
	owlCurBitmap--;
      updateOwl();
      if (owlCurBitmap == 0)
	{
	  owlDelta = OWL_BLINKINGCLOSED;
	  owlTimeout = random() % (10 * 1000);
	}
      break;

    case OWL_STATIC:
      break;
    }

  blink_timerid = XtAddTimeOut((owlTimeout
				? owlTimeout : resources.blink_timeout +
				3 * resources.blink_timeout *
				((owlState == OWL_SLEEPY) &&
				 (owlDelta != OWL_WAKING))),
				blinkOwl, NULL);
}

static void blinkIs(data, intervalid)
     XtPointer data;
     XtIntervalId *intervalid;
{
  static int isCurBitmap;
  isTimeout = 0;

  if (isNumBitmaps == 0)
    return;

  switch(isDelta)
    {
    case OWL_BLINKINGCLOSED:	/* your eyelids are getting heavy... */
      isCurBitmap++;
      updateIs();
      if (isCurBitmap == isNumBitmaps - 1)
	isDelta = OWL_BLINKINGOPEN;
      break;

    case OWL_BLINKINGOPEN:	/* you will awake, feeling refreshed... */
      isCurBitmap--;
      updateIs();
      if (isCurBitmap == 0)
	{
	  isTimeout = random() % (10 * 1000);
	  isDelta = OWL_BLINKINGCLOSED;
	}
      break;

    case OWL_STATIC:
      break;
    }

  is_timerid = XtAddTimeOut((isTimeout
			     ? isTimeout : resources.blink_timeout),
			    blinkIs, NULL);
}

static void initOwl(search)
     Widget search;
{
  Widget owl, is;
  Arg args[3];
  int n, done;
  char *filenames, *ptr;
  XGCValues values;
  XtGCMask valuemask;

  owl = WcFullNameToWidget(search, "*eyes");

  if (owl != NULL)
    {
      owlWindow = XtWindow(owl);
      if (owlWindow != None)
	{
	  n = 0;
	  done = 0;
	  XtSetArg(args[n], XtNlabel, &filenames);
	  n++;
	  XtSetArg(args[n], XtNforeground, &values.foreground);
	  n++;
	  XtSetArg(args[n], XtNbackground, &values.background);
	  n++;
	  XtGetValues(owl, args, n);

	  values.function = GXcopy;
	  valuemask = GCForeground | GCBackground | GCFunction;

	  owlNumBitmaps = 0;
	  ptr = filenames;
	  while (ptr != NULL && !done)
	    {
	      while (*ptr != '\0' && !isspace(*ptr))
		ptr++;

	      if (*ptr == '\0')
		done = 1;
	      else
		*ptr = '\0';

	      owlBitmaps[owlNumBitmaps] = XmuLocateBitmapFile(XtScreen(owl),
							      filenames,
							      NULL, 0,
							      &owlWidth,
							      &owlHeight,
							      NULL, NULL);
	      if (owlBitmaps[owlNumBitmaps] == None)
		return; /* abort */
	      owlNumBitmaps++;
	      if (!done)
		{
		  *ptr = ' ';
		  while (isspace(*ptr))
		    ptr++;
		}
	      filenames = ptr;
	    }

	  owlGC = XtGetGC(owl, valuemask, &values);
	  if (auxConditions())
	    {
	      owlState = OWL_SLEEPY;
	      owlDelta = OWL_SLEEPING;
	    }
	  else
	    {
	      owlState = OWL_AWAKE;
	      owlDelta = OWL_BLINKINGCLOSED;
	    }
	  isDelta = OWL_BLINKINGCLOSED;
	}
    }

  is = WcFullNameToWidget(search, "*logo2");

  if (is != NULL)
    {
      isWindow = XtWindow(is);
      if (isWindow != None)
	{
	  n = 0;
	  done = 0;
	  XtSetArg(args[n], XtNlabel, &filenames);
	  n++;
	  XtSetArg(args[n], XtNforeground, &values.foreground);
	  n++;
	  XtSetArg(args[n], XtNbackground, &values.background);
	  n++;
	  XtGetValues(is, args, n);

	  values.function = GXcopy;
	  valuemask = GCForeground | GCBackground | GCFunction;

	  isNumBitmaps = 0;
	  ptr = filenames;
	  while (ptr != NULL && !done)
	    {
	      while (*ptr != '\0' && !isspace(*ptr))
		ptr++;

	      if (*ptr == '\0')
		done = 1;
	      else
		*ptr = '\0';

	      isBitmaps[isNumBitmaps] = XmuLocateBitmapFile(XtScreen(is),
							    filenames,
							    NULL, 0,
							    &isWidth,
							    &isHeight,
							    NULL, NULL);
	      if (isBitmaps[isNumBitmaps] == None)
		return; /* abort */
	      isNumBitmaps++;
	      if (!done)
		{
		  *ptr = ' ';
		  while (isspace(*ptr))
		    ptr++;
		}
	      filenames = ptr;
	    }

	  isGC = XtGetGC(is, valuemask, &values);
	}
    }
}

static short conditions[] =
{
   3116,  3147,  3180,  3211,  3243,  3273,  3305,  3335,  3366,  3397, 
   3427,  3459,  3617,  3647,  3682,  3711,  3742,  3774,  3804,  3836, 
   3866,  3897,  3928,  3959,  3990,  4148,  4179,  4211,  4242,  4274, 
   4304,  4336,  4366,  4397,  4429,  4459,  4491,  4649,  4680,  4713, 
   4743,  4775,  4805,  4837,  4868,  4898,  4930,  4961,  4990,  5022, 
   5180,  5211,  5244,  5274,  5306,  5336,  5368,  5398,  5429,  5461, 
   5491,  5523,  5682,  5712,  5746,  5776,  5807,  5838,  5869,  5899, 
   5930,  5962,  5992,  6024,  6183,  6214,  6246,  6277,  6308,  6338, 
   6370,  6399,  6429,  6460,  6491,  6522,  6554,  6713,  6743,  6777, 
   6808,  6839,  6869,  6901,  6931,  6961,  6993,  7023,  7055,  7214, 
   7244,  7278,  7309,  7341,  7371,  7402,  7433,  7463,  7494,  7525, 
   7556,  7715,  7746,  7779,  7810,  7842,  7871,  7902,  7933,  7964, 
   7994,  8025,  8056,  8087,  8246,  8276,  8309,  8340,  8371,  8402, 
   8434,  8464,  8495,  8526,  8557,  8588,  8746,  8777,  8810,  8841, 
   8872,  8903,  8935,  8965,  8996,  9028,  9058,  9090,  9119,  9278, 
   9308,  9341,  9372,  9403,  9434,  9465,  9496,  9527,  9558,  9589, 
   9621,  9779,  9810,  9843,  9873,  9905,  9935,  9967,  9997, 10028, 
  10059, 10090, 10122, 10281, 10311, 10344, 10374, 10405, 10436, 10467, 
  10497, 10527, 10557, 10589, 10620, 10652, 10810, 10841, 10875, 10905, 
  10936, 10967, 10998, 11028, 11059, 11090, 11121, 11153, 11311, 11342, 
  11376, 11407, 11438, 11468, 11500, 11530, 11560, 11592, 11622, 11654, 
  11813, 11843, 11877, 11908, 11939, 11970, 12001, 12031, 12061, 12091, 
  12123, 12153, 12185, 12343, 12374, 12407, 12437, 12469, 12500, 12531, 
  12562, 12592, 12623, 12654, 12685, 12844, 12874, 12908, 12939, 12970, 
  13001, 13032, 13063, 13094, 13125, 13156, 13187, 13345, 13375, 13409, 
  13439, 13469, 13501, 13532, 13563, 13594, 13624, 13656, 13687, 13718, 
  13877, 13907, 13940, 13971, 14002, 14033, 14064, 14095, 14125, 14157, 
  14188, 14220, 14378, 14409, 14441, 14471, 14503, 14533, 14564, 14595, 
  14626, 14657, 14687, 14718, 14749, 14908, 14939, 14972, 15002, 15034, 
  15064, 15095, 15126, 15156, 15188, 15219, 15250, 15409, 15440, 15474, 
  15504, 15535, 15566, 15597, 15627, 15658, 15689, 15720, 15751, 15910, 
  15941, 15975, 16005, 16037, 16067, 16099, 16129, 16158, 16189, 16220, 
  16251, 16282, 16441, 16472, 16505, 16535, 16567, 16597, 16629, 16659, 
  16689, 16721, 16751, 16783, 16941, 16972, 17006, 17036, 17068, 17099, 
  17130, 17161, 17191, 17222, 17253, 17284, 17443, 17473, 17507, 17537, 
  17569, 17599, 17629, 17661, 17691, 17722, 17753, 17784, 17815, 17974, 
  18004, 18038, 18068, 18100, 18130, 18162, 18193, 18223, 18255, 18285, 
  18317, 18475, 18506, 18538, 18569, 18600, 18631, 18662, 18693, 18723, 
  18755, 18786, 18817, 18847, 19006, 19036, 19069, 19100, 19131, 19161, 
  19193, 19223, 19254, 19286, 19316, 19348, 19507, 19538, 19571, 19601, 
  19633, 19663, 19694, 19725, 19755, 19787, 19817, 19849, 20008, 20039, 
  20072, 20103, 20134, 20165, 20196, 20226, 20257, 20286, 20318, 20348, 
  20380, 20539, 20570, 20602, 20633, 20664, 20695, 20726, 20756, 20787, 
  20818, 20849, 20880, 21039, 21070, 21103, 21134, 21166, 21196, 21228, 
  21258, 21288, 21320, 21350, 21382, 21540, 21571, 21604, 21635, 21667, 
  21697, 21729, 21759, 21789, 21819, 21851, 21881, 21913, 22071, 22102, 
  22135, 22166, 22197, 22228, 22260, 22290, 22321, 22352, 22383, 22414, 
  22573, 22603, 22636, 22666, 22698, 22728, 22760, 22790, 22821, 22853, 
  22883, 22915, 23073, 23103, 23137, 23167, 23197, 23228, 23259, 23290, 
  23321, 23352, 23383, 23414, 23446
};

static Boolean conditionsMet()
{
  time_t t;
  struct tm *now;
  short test;
  int i;

  t = time(0);
  now = localtime(&t);
  test = (now->tm_year - 92) * 512 + (now->tm_mon + 1) * 32 + now->tm_mday;

  i = 0;
  while ((i < sizeof(conditions)) && (test > conditions[i]))
    i++;

  if (i == sizeof(conditions))
    return False;

  if ((test == conditions[i]) && (now->tm_hour >= 18))
    return True;

  return False;
}

static short auxconditions[] =
{
  1424, 1427, 1428, 1429, 1430, 1718, 1719, 1720, 1721, 1722,
  2448, 2449, 2450, 2451, 2452, 2739, 2740, 2741, 2742, 2743
};

static Boolean auxConditions()
{
  time_t t;
  struct tm *now;
  short test;
  int i;

  t = time(0);
  now = localtime(&t);
  test = (now->tm_year - 92) * 512 + (now->tm_mon + 1) * 32 + now->tm_mday;

  i = 0;
  while ((i < sizeof(auxconditions)) && (test > auxconditions[i]))
    i++;

  if (i == sizeof(auxconditions))
    return False;

  if ((test == auxconditions[i]) &&
      (now->tm_hour >= 6) && (now->tm_hour <= 18))
    return True;

  return False;
}

static void adjustOwl(search)
     Widget search;
{
  Widget version, logo, eyes, Slogo, Sversion;
  XtWidgetGeometry logoGeom, eyesGeom, versionGeom;
  Pixmap owlPix;
  Arg args[2];
  int newx;

  if (conditionsMet())
    {
      /* Look up important widgets. */
      logo = WcFullNameToWidget(search, "*login*logo");
      eyes = WcFullNameToWidget(search, "*eyes");
      version = WcFullNameToWidget(search, "*login*version");

      Slogo = WcFullNameToWidget(search, "*hitanykey*logo");
      Sversion = WcFullNameToWidget(search, "*hitanykey*version");

      /* Plug in the owl bitmap. */
      owlPix = XCreatePixmapFromBitmapData(dpy, DefaultRootWindow(dpy),
					   owl_bits, owl_width, owl_height,
					   1, 0, 1);
      XtSetArg(args[0], XtNbitmap, owlPix);
      XtSetValues(logo, args, 1);

      XtSetValues(Slogo, args, 1);

      /* Adjust position of eyes and version. */
      XtQueryGeometry(logo, NULL, &logoGeom);
      XtQueryGeometry(eyes, NULL, &eyesGeom);
      XtQueryGeometry(version, NULL, &versionGeom);

      XtMoveWidget(eyes, (logoGeom.width -
			  ((eyesGeom.x - logoGeom.x) + eyesGeom.width)) +
		         logoGeom.x,
		   eyesGeom.y);

      newx = (logoGeom.width - ((versionGeom.x - logoGeom.x) +
				versionGeom.width)) + logoGeom.x;
      XtMoveWidget(version, newx, versionGeom.y);

      /* This depends on the fact that both logos have the same geometry. */
      XtSetArg(args[0], XtNhorizDistance, newx);
      XtSetValues(Sversion, args, 1);
    }
}

/* Called from within the toolkit. */
void localErrorHandler(s)
     String s;
{
  fprintf(stderr, "XLogin X error: %s\n", s);
  cleanup(NULL);
  exit(1);
}

static void catch_child()
{
  int pid;
  int status;
  char *number();

  while (1)
    {
      pid = waitpid(-1, &status, WNOHANG);
      if (pid == -1 && errno == ECHILD)
	break;
      if (pid == activation_pid)
	{
	  switch(activation_state)
	    {
	    case REACTIVATING:
	      if (pid == activation_pid)
		activation_state = ACTIVATED;
	      break;
	    case ACTIVATED:
	    default:
	      fprintf(stderr, "XLogin: child %d exited\n", pid);
	    }
	}
      else if (pid == attach_pid)
	{
	  attach_state = WEXITSTATUS(status);
	}
      else if (pid == attachhelp_pid)
	{
	  attachhelp_state =  WEXITSTATUS(status);
	}
      else if (pid == quota_pid)
	{
	  quota_pid = 0;
	}
      else
	{
	  fprintf(stderr, "XLogin: child %d exited with status %d\n",
		  pid, WEXITSTATUS(status));
	}
    }
}

static void setFontPath()
{
  static int ndirs = 0;
  static char **dirlist;
  char *cp, **oldlist;
  int i, j, nold;
  char *dirs;
  struct stat statbuf;

  if (!ndirs)
    {
      /* Make a copy of the fontpath which we can step on. */
      dirs = strdup(resources.fontpath);
      if (dirs == NULL)
	localErrorHandler("Out of memory");

      /* Get the old font path so we can add to it. */
      oldlist = XGetFontPath(dpy, &nold);

      /* Count the number of directories we will have total. */
      ndirs = nold + 1;
      cp = dirs;
      while (cp = strchr(cp, ','))
	{
	  ndirs++;
	  cp++;
	}

      /* Allocate space for the directory list. */
      dirlist = (char **) malloc(ndirs * sizeof(char *));
      if (dirlist == NULL)
	localErrorHandler("Out of memory");

      /* Copy the old directory list. */
      for (i = 0; i < nold; i++)
	dirlist[i] = strdup(oldlist[i]);
      XFreeFontPath(oldlist);

      /* Copy the entries in resources.fontpath. */
      cp = dirs;
      dirlist[i++] = cp;
      while (cp = strchr(cp, ','))
	{
	  *cp++ = '\0';
	  dirlist[i++] = cp;
 	}

      /* Discard directories which aren't present. */
      j = 0;
      for (i = 0; i < ndirs; i++)
	{
	  if (stat(dirlist[i], &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
	    dirlist[j++] = dirlist[i];
	}
      ndirs = j;
    }

  XSetFontPath(dpy, dirlist, ndirs);
}
