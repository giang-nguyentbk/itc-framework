#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>

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
	struct msgbuf sbuf;
	size_t len;

	if(argc != 3)
	{
		(void)fprintf(stderr, "Usage: ./send <proj_id> <message>\n");
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

	if((msqid = msgget(key, IPC_CREAT | 0666)) < 0)
	{
		perror("msgget");
		exit(EXIT_FAILURE);
	}

	sbuf.mtype = 1;

	(void)strncpy(sbuf.mtext, argv[2], MSGSZ);

	len = strlen(sbuf.mtext);

	if(msgsnd(msqid, &sbuf, len, IPC_NOWAIT) < 0)
	{
		(void)fprintf(stderr, "msqid = %d, sbuf.mtype = %ld, sbuf.mtext = %s, len = %d\n", \
			msqid, sbuf.mtype, sbuf.mtext, (int)len);
		perror("msgsnd");
		exit(EXIT_FAILURE);
	}

	(void)printf("Sent: %s\n", sbuf.mtext);
	exit(EXIT_SUCCESS);
}