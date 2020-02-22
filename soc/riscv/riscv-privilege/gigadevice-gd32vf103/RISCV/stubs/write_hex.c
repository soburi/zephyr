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
#include <stdint.h>
#include <unistd.h>

void write_hex(int fd, unsigned long int hex)
{
  uint8_t ii;
  uint8_t jj;
  char towrite;
  write(fd , "0x", 2);
  for (ii = sizeof(unsigned long int) * 2 ; ii > 0; ii--) {
    jj = ii - 1;
    uint8_t digit = ((hex & (0xF << (jj*4))) >> (jj*4));
    towrite = digit < 0xA ? ('0' + digit) : ('A' +  (digit - 0xA));
    write(fd, &towrite, 1);
  }
}
#endif
