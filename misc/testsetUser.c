#include <stdio.h>
#include "nannylib.h"

int main(int argc, char **argv, char **env)
{
  int ret;
  char *args[3];

  args[0] = "arg1";
  args[1] = "arg2";
  args[2] = NULL;

  ret = nanny_setupUser(argv[1] ? argv[1] : "root", env, args);
  if (ret)
    {
      fprintf(stderr, "setupUser failed.\n");
      exit(1);
    }

  exit(0);
}
