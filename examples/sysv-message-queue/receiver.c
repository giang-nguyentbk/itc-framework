#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/types.h>

#define MSGSZ	128
#define SERVER_KEY_PATHNAME	"/tmp/itccoord/sysvmsq"

struct msgbuf {
	long mtype;
	char mtext[MSGSZ];
};

int main(int argc, char** argv)
{
	int msqid;
	int proj_id;
	key_t key;
	struct msgbuf rbuf;

	if(argc != 2)
	{
		(void)fprintf(stderr, "Usage: ./receive <key>\n");
		exit(EXIT_FAILURE);
	}

	if((proj_id = atoi(argv[1])) < 1)
	{
		(void)fprintf(stderr, "Invalid proj_id: %d!\n", proj_id);
		exit(EXIT_FAILURE);
	}

	if ((key = ftok(SERVER_KEY_PATHNAME, proj_id)) == -1)
	{
		perror ("ftok");
		exit (EXIT_FAILURE);
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