#if 0
/* See LICENSE of license details. */

typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int pid_t;
typedef unsigned int _ssize_t;
typedef unsigned int useconds_t;
typedef unsigned int sseconds_t;
#define __BEGIN_DECLS
#define __END_DECLS
#define __dead2 { }
#include <unistd.h>

int _isatty(int fd)
{
  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    return 1;

  return 0;
}
#endif
