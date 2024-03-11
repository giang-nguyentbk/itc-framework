#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/types.h>

#define MSGSZ	128

struct msgbuf {
	long mtype;
	char mtext[MSGSZ];
};

int main(int argc, char** argv)
{
	int msqid;
	key_t key;
	struct msgbuf rbuf;

	if(argc != 2)
	{
		(void)fprintf(stderr, "Usage: ./send <key>\n");
		exit(EXIT_FAILURE);
	}

	if((key = atoi(argv[1])) < 1)
	{
		(void)fprintf(stderr, "Invalid key: %d!\n", key);
		exit(EXIT_FAILURE);
	}

	if((msqid = msgget(key, 0)) < 0)
	{
		perror("msgget");
		exit(EXIT_FAILURE);
	}

	if(msgrcv(msqid, &rbuf, MSGSZ, 1, 0) < 0)
	{
		(void)fprintf(stderr, "msqid = %d, rbuf.mtype = %ld, rbuf.mtext = %s\n", \
			msqid, rbuf.mtype, rbuf.mtext);
		perror("msgrcv");
		exit(EXIT_FAILURE);
	}

	(void)printf("Received: %s\n", rbuf.mtext);
	exit(EXIT_SUCCESS);
}