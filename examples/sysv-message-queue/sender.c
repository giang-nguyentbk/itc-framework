#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ipc.h>
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
	struct msgbuf sbuf;
	size_t len;

	if(argc != 3)
	{
		(void)fprintf(stderr, "Usage: ./send <key> <message>\n");
		exit(EXIT_FAILURE);
	}

	if((key = atoi(argv[1])) < 1)
	{
		(void)fprintf(stderr, "Invalid key: %d!\n", key);
		exit(EXIT_FAILURE);
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