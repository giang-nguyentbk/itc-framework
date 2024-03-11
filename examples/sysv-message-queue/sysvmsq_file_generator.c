#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/stat.h>

#define SERVER_KEY_PATHNAME	"/tmp/sysvmsq/sysvmsq_file"

int main(void)
{
	FILE* fd;
	int res;

	res = mkdir("/tmp/sysvmsq/", 0777);
	if(res < 0 && errno != EEXIST)
	{
		perror ("mkdir");
		exit (EXIT_FAILURE);
	}

	if(errno != EEXIST)
	{
		res = chmod("/tmp/sysvmsq/", 0777);
		if(res < 0)
		{
			perror ("chmod");
			exit (EXIT_FAILURE);
		}

		fd = fopen(SERVER_KEY_PATHNAME, "w");
		if(fd == NULL)
		{
			perror ("fopen");
			exit (EXIT_FAILURE);
		}

		if(fclose(fd) != 0)
		{
			perror ("fclose");
			exit (EXIT_FAILURE);
		}
	}

	(void)printf("Created: %s\n", SERVER_KEY_PATHNAME);
	exit(EXIT_SUCCESS);
}