#include <stdio.h>
#include <linux/version.h>
#include <sys/utsname.h>

/*
 * from rhel7's linux/version.h:
 *   #define LINUX_VERSION_CODE 199168
 *   #define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
 */

int main(void)
{
  int maj = LINUX_VERSION_CODE >> 16;
  int min = ( LINUX_VERSION_CODE - ( maj << 16 ) ) >> 8;
  int pat = LINUX_VERSION_CODE - ( maj << 16 ) - ( min << 8 );
  struct utsname unamedata;

  printf("linux/version.h : %d = %d.%d.%d\n", LINUX_VERSION_CODE, maj, min, pat);

  uname(&unamedata);
  printf("uname : utsname.release = %s\n", unamedata.release);

  return(0);
}