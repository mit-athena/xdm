#include <stdlib.h>
#include <syslog.h>
#include "nanny.h"
#include "pc.h"
#include "var.h"
#include "cvt.h"

#define C(stuff) if (stuff) goto cleanup

int nanny_exchangeVars(varlist *vin, varlist **vout)
{
  pc_state *ps = NULL;
  pc_port *outport = NULL;
  pc_message messageIn, *messageOut = NULL;
  buffer *bufIn = NULL, bufOut;
  char *value;
  int ret;

  if (vin == NULL || vout == NULL)
    return 1;

  C(cvt_vars2buf(&bufIn, vin));

  messageIn.data = bufIn->buf;
  messageIn.length = bufIn->len;

  C(pc_init(&ps));

  while (1)
    {
      C(pc_openport(&outport, NANNYPORT)); /* create outport */

      messageIn.source = outport;
      C(pc_send(&messageIn));

      pc_addport(ps, outport);
      C(pc_wait(&messageOut, ps));	/* create messageOut */
      if (messageOut->type == PC_BROKEN)
	{
	  syslog(LOG_ERR, "nannylib: pc_wait returns %m");
	  C(1);
	}

      if (messageOut->type != PC_DATA)
	{
	  syslog(LOG_ERR, "nannylib: pc_wait returned unexpected type %d",
		 messageOut->type);
	  C(1);
	}

      if (messageOut->data == NULL)
	{
	  syslog(LOG_ERR, "nannylib: pc_wait returned NULL data");
	  C(1);
	}

      pc_removeport(ps, outport);
      pc_close(outport);		outport = NULL;

      bufOut.buf = messageOut->data;
      bufOut.len = messageOut->length;

      C(var_init(vout));		/* create *vout */
      C(cvt_buf2vars(*vout, &bufOut));

      pc_freemessage(messageOut);	messageOut = NULL;

      if (var_getString(*vout, N_RETRY, &value) ||
	  !strcmp(value, N_FALSE))
	{
	  pc_destroy(ps);
	  free(bufIn->buf);
	  free(bufIn);
	  return 0;
	}

      var_destroy(*vout);		*vout = NULL;
      sleep(1);
    }

  cleanup:
    if (messageOut)
      pc_freemessage(messageOut);
    if (bufIn)
      {
	free(bufIn->buf);
	free(bufIn);
      }
    if (outport)
      pc_close(outport);
    if (ps)
      pc_destroy(ps);
    if (*vout)
      {
	var_destroy(*vout);
	*vout = NULL;
      }

    return 1;
}

int nanny_setupUser(char *name, char **env, char **args)
{
  varlist *vsend = NULL, *vget = NULL;
  buffer *buf = NULL;
  char *value;

  if (name == NULL || env == NULL || args == NULL)
    return 1;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_USER, name));

  C(cvt_strings2buf(&buf, env));
  C(var_setValue(vsend, N_ENV, buf->buf, buf->len));
  free(buf->buf);  free(buf);  buf = NULL;

  C(cvt_strings2buf(&buf, args));
  C(var_setValue(vsend, N_XSESSARGS, buf->buf, buf->len));
  free(buf->buf);  free(buf);  buf = NULL;

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_USER, &value));
  if (strcmp(value, N_OK))
    {
      var_destroy(vget);
      return 1;
    }

  var_destroy(vget);
  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (buf)
    {
      if (buf->buf)
	free(buf->buf);
      free(buf);
    }
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_loginUser(char ***env, char ***args, char **tty)
{
  varlist *vsend = NULL, *vget = NULL;
  buffer buf;
  char *value;
  int len;

  if (env == NULL || args == NULL || tty == NULL)
    return 1;

  *env = NULL;
  *args = NULL;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_LOGGED_IN, N_TRUE));
  C(var_setString(vsend, N_ENV, cvt_query));
  C(var_setString(vsend, N_XSESSARGS, cvt_query));
  C(var_setString(vsend, N_TTY, cvt_query));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(cvt_var2strings(vget, N_ENV, env));
  C(cvt_var2strings(vget, N_XSESSARGS, args));

  C(var_getString(vget, N_TTY, &value));
  C(!(*tty = malloc(strlen(value) + 1)));
  strcpy(*tty, value);

  C(var_getString(vget, N_LOGGED_IN, &value));
  C(strcmp(value, "logged in")); /* XXX should be some constant */

  var_destroy(vget);
  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);
  if (*env)
    cvt_freeStrings(*env);
  if (*args)
    cvt_freeStrings(*args);
  return 1;
}

int nanny_logoutUser()
{
  varlist *vsend = NULL, *vget = NULL;
  buffer buf;
  char *value;
  int len;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_LOGGED_IN, N_FALSE));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  /* XXX Check "LOGGED_IN=OK"... or something */

  var_destroy(vget);
  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_getTty(char *tty, int ttylen)
{
  varlist *vsend = NULL, *vget = NULL;
  char *value;

  if (tty == NULL)
    return 1;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_TTY, cvt_query));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_TTY, &value));

  strncpy(tty, value, ttylen);
  var_destroy(vget);

  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_getNannyPid(int *pid)
{
  varlist *vsend = NULL, *vget = NULL;
  char *value;

  if (pid == NULL)
    return 1;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_PID, cvt_query));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_PID, &value));

  *pid = atoi(value);
  var_destroy(vget);

  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_setConsoleMode()
{
  varlist *vsend = NULL, *vget = NULL;
  char *value;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_MODE, N_CONSOLE));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_MODE, &value));

  if (strcmp(value, N_OK))
    {
      var_destroy(vget);
      return 1;
    }

  var_destroy(vget);

  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_setXConsolePref(int on)
{
  varlist *vsend = NULL, *vget = NULL;
  char *value;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_CONSPREF, on ? N_ON : N_OFF));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_CONSPREF, &value));

  var_destroy(vget);

  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}

int nanny_restartX()
{
  varlist *vsend = NULL, *vget = NULL;
  char *value;

  if (var_init(&vsend))
    return 1;

  C(var_setString(vsend, N_RESTARTX, N_TRUE));

  C(nanny_exchangeVars(vsend, &vget));
  var_destroy(vsend);  vsend = NULL;

  C(var_getString(vget, N_RESTARTX, &value));

  if (strcmp(value, N_OK))
    {
      var_destroy(vget);
      return 1;
    }

  var_destroy(vget);

  return 0;

 cleanup:
  if (vsend)
    var_destroy(vsend);
  if (vget)
    var_destroy(vget);

  return 1;
}
