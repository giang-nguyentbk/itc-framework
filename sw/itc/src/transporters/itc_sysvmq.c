

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include <errno.h>
#include <search.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"
#include "itc_threadmanager.h"


/*****************************************************************************\/
*****                    INTERNAL TYPES IN SYSV-ATOR                       *****
*******************************************************************************/
#define ITC_SYSV_MSG_BASE	(ITC_MSG_BASE + 0x100)
#define ITC_SYSV_MSQ_TX_MSG	(ITC_SYSV_MSG_BASE + 1)

struct sysvmq_contactlist {
	itc_mbox_id_t	mbox_id_in_itccoord;
	int		sysvmq_id;
};

struct sysvmq_instance {
	itc_mbox_id_t           	my_mbox_id_in_itccoord;
        itc_mbox_id_t           	itccoord_mask;
        itc_mbox_id_t           	itccoord_shift;

	itc_mbox_id_t			my_mbox_id;
	int				my_sysvmq_id;

	pid_t				pid;
	pthread_mutex_t			thread_mtx;
	pthread_key_t			destruct_key;

	int				is_initialized;
	int				is_terminated;
	int				max_msgsize;
	char*				rx_buffer;

	struct sysvmq_contactlist	sysvmq_cl[MAX_SUPPORTED_PROCESSES];
};



/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct sysvmq_instance sysvmq_inst; // One instance per a process, multiple threads all use this one.




/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_sysvmq_resources(struct result_code* rc);
static void generate_msqfile(struct result_code* rc);
static struct sysvmq_contactlist* get_sysvmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static struct sysvmq_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static void add_sysvmq_cl(struct result_code* rc, struct sysvmq_contactlist* cl, itc_mbox_id_t mbox_id);
static int get_sysvmq_id(struct result_code* rc, itc_mbox_id_t mbox_id);
static void remove_sysvmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static void forward_sysvmq_msg(struct result_code* rc, char* buffer, int length, int msqid);
static void rxthread_destructor(void* data);




/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void sysvmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags);

static void sysvmq_exit(struct result_code* rc);


static void sysvmq_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static int sysvmq_maxmsgsize(struct result_code* rc);

static void* sysvmq_rx_thread(void *data);

struct itci_transport_apis sysvmq_trans_apis = { NULL,
                                            	sysvmq_init,
                                            	sysvmq_exit,
                                            	NULL,
                                            	NULL,
                                            	sysvmq_send,
                                            	NULL,
                                            	NULL,
                                            	sysvmq_maxmsgsize };




/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
void sysvmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;

	if(sysvmq_inst.is_initialized != 0)
	{
		if(flags & ITC_FLAGS_FORCE_REINIT)
		{
			printf("\tDEBUG: sysvmq_init -Force re-initializing!\n");
			release_sysvmq_resources(rc);
			if(rc != ITC_OK)
			{
				printf("\tDEBUG: sysvmq_init - Failed to release_sysvmq_resources!\n");
				return;
			}
			(void)sysvmq_maxmsgsize(rc);
		} else
		{
			printf("\tDEBUG: sysvmq_init - Already initialized!\n");
			rc->flags |= ITC_ALREADY_INIT;
			return;
		}
	}

	generate_msqfile(rc);
	if(rc->flags != ITC_OK)
	{
		printf("\tDEBUG: sysvmq_init - Failed to generate msqfile!\n");
		return;
	}

	// Calculate itccoord_shift value
	int tmp_shift, tmp_mask;
	tmp_shift = 0;
	tmp_mask = itccoord_mask;
	while(!(tmp_mask & 0x1))
	{
		tmp_mask = tmp_mask >> 1;
		tmp_shift++; // Should be 20 currently
	}

	sysvmq_inst.my_sysvmq_id		= -1; // Will be only specified when rx thread starts
	sysvmq_inst.pid				= getpid();
	sysvmq_inst.itccoord_mask 		= itccoord_mask;
	sysvmq_inst.itccoord_shift		= tmp_shift;
	sysvmq_inst.my_mbox_id_in_itccoord	= my_mbox_id_in_itccoord;

	// Create key for thread-specific data (mbox_id)
	int ret = pthread_key_create(&sysvmq_inst.destruct_key, rxthread_destructor);
	if(ret != 0)
	{
		printf("\tDEBUG: sysvmq_init - pthread_key_create error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	ret = pthread_mutex_init(&sysvmq_inst.thread_mtx, NULL);
	if(ret != 0)
	{
		printf("\tDEBUG: sysvmq_init - pthread_mutex_init error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	add_itcthread(rc, sysvmq_rx_thread, NULL, true, &sysvmq_inst.thread_mtx);
}

static void sysvmq_exit(struct result_code* rc)
{
	if(!sysvmq_inst.is_initialized)
	{
		printf("\tDEBUG: sysvmq_exit - Not initialized yet!\n");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	int ret = pthread_mutex_destroy(&sysvmq_inst.thread_mtx);
	if(ret != 0)
	{
		printf("\tDEBUG: sysvmq_exit - pthread_mutex_destroy error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	ret = pthread_key_delete(sysvmq_inst.destruct_key);
	if(ret != 0)
	{
		printf("\tDEBUG: sysvmq_exit - pthread_key_delete error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	memset(&sysvmq_inst, 0, sizeof(struct sysvmq_instance));
}

static int sysvmq_maxmsgsize(struct result_code* rc)
{
	// struct msginfo from <bits/msg.h> included in <sys/msg.h>
	struct msginfo info;

	if(sysvmq_inst.max_msgsize > 0)
	{
		printf("\tDEBUG: sysvmq_maxmsgsize - Get max msg size successfully, max_msgsize = %u!\n", sysvmq_inst.max_msgsize);
		return sysvmq_inst.max_msgsize;
	}

	if(msgctl(0, IPC_INFO, (struct msqid_ds*)&info) == -1)
	{
		// ERROR tracing is needed only, no need to set result code
		perror("\tDEBUG: sysvmq_maxmsgsize - msgctl");
		rc->flags |= ITC_SYSCALL_ERROR;
	}

	sysvmq_inst.max_msgsize = MIN(info.msgmax, info.msgmnb);

	return sysvmq_inst.max_msgsize;
}

static void sysvmq_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to)
{
	union itc_msg* msg;
	struct sysvmq_contactlist* cl;
	int size;
	long* txmsg;

	if(!sysvmq_inst.is_initialized)
	{
		printf("\tDEBUG: sysvmq_send - Already initialized!\n");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	cl = get_sysvmq_cl(rc, to);
	if(cl == NULL || cl->mbox_id_in_itccoord == 0)
	{
		printf("\tDEBUG: sysvmq_send - Receiver side not initialised message queue yet!\n");
		rc->flags &= ~ITC_SYSCALL_ERROR; // The receiver side has not initialised message queue yet
		rc->flags |= ITC_QUEUE_NULL; // So remove unecessary syscall error, return an ITC_QUEUE_NULL warning instead. This is not an ERROR at all!
		/* If send failed, users have to self-free the message. ITC system only free messages when send successfully */
		return;
	}

	size = message->size + ITC_HEADER_SIZE; // We need not to send ENDPOINT
	txmsg = (long*)malloc(sizeof(long) + size);
	*txmsg = ITC_SYSV_MSQ_TX_MSG;
	memcpy((void*)(txmsg + 1), message, size);

	while(msgsnd(cl->sysvmq_id, (void*)txmsg, size, MSG_NOERROR) == -1)
	{
		if(errno == EINTR)
		{
			continue;
		} else if(errno == EINVAL || errno == EIDRM)
		{
			printf("\tDEBUG: sysvmq_send - MSG queue of receiver has corrupted and just re-created, add contact list and resend msg again!\n");
			remove_sysvmq_cl(rc, to);
			add_sysvmq_cl(rc, cl, to);
			if(cl->mbox_id_in_itccoord == 0)
			{
				printf("\tDEBUG: sysvmq_send - Add contact list again failed due to msq_key = -1!\n");
				break;
			}
		} else
		{
			// ERROR trace is needed here
			perror("\tDEBUG: sysvmq_send - msgsnd");
			rc->flags |= ITC_SYSCALL_ERROR; // Will not return here
		}
	}

	free(txmsg);

#ifdef UNITTEST
	free(message);
	(void)msg; // Avoid gcc compiler warning unused of msg in UNITTEST scenario.
#else
	msg = CONVERT_TO_MSG(message);
	itc_free(&msg);
#endif
}

static void* sysvmq_rx_thread(void *data)
{
	(void)data;

	key_t key;
	char itc_mbox_name[30];
	ssize_t rx_len = 0;
	uint8_t repeat = 0;
	struct msqid_ds msqinfo;
	int proj_id;
	struct result_code* rc_tmp;
	struct result_code rc_tmp_stack;

	if(prctl(PR_SET_NAME, "itc_rx_sysvmq", 0, 0, 0) == -1)
	{
		// ERROR trace is needed here
		perror("\tDEBUG: sysvmq_rx_thread - prctl");
		return NULL;
	}

	sprintf(itc_mbox_name, "itc_rx_sysvmq_0x%08x", sysvmq_inst.my_mbox_id_in_itccoord);
	

#ifdef SYSVMQ_TRANS_UNITTEST
	// Simulate that everything is ok at this point. Do nothing in unit test.
	// API itc_create_mailbox is an external interface, so do not care about it if everything we pass into it is all correct.
	sysvmq_inst.my_mbox_id = 1;
#else
	sysvmq_inst.my_mbox_id = itc_create_mailbox(itc_mbox_name, ITC_NO_NAMESPACE);
#endif

	printf("\tDEBUG: sysvmq_rx_thread - Starting sysvmq_rx_thread %s...!\n", itc_mbox_name);
	int ret = pthread_setspecific(sysvmq_inst.destruct_key, (void*)(unsigned long)sysvmq_inst.my_mbox_id);
	if(ret != 0)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: sysvmq_rx_thread - pthread_setspecific error code = %d\n", ret);
		return NULL;
	}

	proj_id = (sysvmq_inst.my_mbox_id_in_itccoord >> sysvmq_inst.itccoord_shift);
	key = ftok(ITC_SYSVMSQ_FILENAME, proj_id);
	if(key == -1)
	{
		// ERROR trace is needed here
		// Will not return
		perror("\tDEBUG: sysvmq_rx_thread - ftok");
	}

	sysvmq_inst.my_sysvmq_id = msgget(key, IPC_CREAT | 0666);
	if(sysvmq_inst.my_sysvmq_id == -1)
	{
		// ERROR trace is needed here
		// Will not return
		perror("\tDEBUG: sysvmq_rx_thread - msgget");
	}

	if(msgctl(sysvmq_inst.my_sysvmq_id, IPC_STAT, &msqinfo) == -1)
	{
		// ERROR trace is needed here
		// Will not return
		perror("\tDEBUG: sysvmq_rx_thread - msgctl");
	}

	if(msqinfo.msg_qnum != 0)
	{
		// Queue should empty, if not remove it and re-create
		if(msgctl(sysvmq_inst.my_sysvmq_id, IPC_RMID, &msqinfo) == -1)
		{
			// ERROR trace is needed here
			// Will not return
			perror("\tDEBUG: sysvmq_rx_thread - msgctl (Recreate queue)");
		}

		sysvmq_inst.my_sysvmq_id = msgget(key, IPC_CREAT | 0666);
		if(sysvmq_inst.my_sysvmq_id == -1)
		{
			// ERROR trace is needed here
			// Will not return
			perror("\tDEBUG: sysvmq_rx_thread - msgget (Recreate queue)");
		}
	}

	rc_tmp = (struct result_code*)malloc(sizeof(struct result_code));
	(void)sysvmq_maxmsgsize(rc_tmp);
	sysvmq_inst.rx_buffer = malloc(sysvmq_inst.max_msgsize);
	if(sysvmq_inst.rx_buffer == NULL)
	{
		// ERROR trace is needed here
		perror("\tDEBUG: sysvmq_rx_thread - malloc");
		free(rc_tmp);
		return NULL;
	}
	memset(sysvmq_inst.rx_buffer, 0, sysvmq_inst.max_msgsize);

	MUTEX_UNLOCK(&sysvmq_inst.thread_mtx);
	free(rc_tmp);

	for(;;)
	{
		rx_len = msgrcv(sysvmq_inst.my_sysvmq_id, sysvmq_inst.rx_buffer, sysvmq_inst.max_msgsize - sizeof(long), 0, 0);
		if(sysvmq_inst.is_terminated)
		{
			printf("\tDEBUG: sysvmq_rx_thread - Terminating sysvmq rx thread!\n");
			break;
		}

		if(rx_len < 0)
		{
			if((errno == EIDRM || errno == EINVAL) && !repeat)
			{
				/* Give it one more retry after 10ms, if problem still persists, ERROR trace is needed */
				printf("\tDEBUG: sysvmq_rx_thread - Retry one more time!\n");
				usleep(10000);
				repeat = 1;
				continue;
			} else if(errno == EINTR)
			{
				continue;
			}

			// ERROR trace is needed here
			printf("\tDEBUG: sysvmq_rx_thread - Negative rx message length, rx_len = %ld!\n", rx_len);
		}

		forward_sysvmq_msg(&rc_tmp_stack, sysvmq_inst.rx_buffer, rx_len + sizeof(long), sysvmq_inst.my_sysvmq_id);
		repeat = 0;
	}
	
	return NULL;
}

/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_sysvmq_resources(struct result_code* rc)
{
	int ret = pthread_key_delete(sysvmq_inst.destruct_key);
	if(ret != 0)
	{
		printf("\tDEBUG: release_sysvmq_resources - pthread_key_delete error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	memset(&sysvmq_inst, 0, sizeof(struct sysvmq_instance));
}

static void generate_msqfile(struct result_code* rc)
{
	FILE* fd;
	int res;

	res = mkdir(ITC_BASE_PATH, 0777);

	if(res < 0 && errno != EEXIST)
	{
		perror("\tDEBUG: generate_msqfile - mkdir /tmp/itc/");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = mkdir(ITC_SYSVMSQ_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		perror("\tDEBUG: generate_msqfile - mkdir /tmp/itc/sysvmq/");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVMSQ_FOLDER, 0777);
	if(res < 0)
	{
		perror("\tDEBUG: generate_msqfile - chmod");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	fd = fopen(ITC_SYSVMSQ_FILENAME, "w");
	if(fd == NULL)
	{
		perror("\tDEBUG: generate_msqfile - fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	printf("\tDEBUG: generate_msqfile - Open file %s successfully!\n", ITC_SYSVMSQ_FILENAME);

	if(fclose(fd) != 0)
	{
		perror("\tDEBUG: generate_msqfile - fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvmq_inst.is_initialized = 1;
}

static struct sysvmq_contactlist* get_sysvmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	struct sysvmq_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	if(cl == NULL)
	{
		printf("\tDEBUG: get_sysvmq_cl - Contact list not found!\n");
		return NULL;
	}

	if(cl->mbox_id_in_itccoord == 0)
	{
		printf("\tDEBUG: get_sysvmq_cl - Add contact list!\n");
		add_sysvmq_cl(rc, cl, mbox_id);
	}

	return cl;
}

static struct sysvmq_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t pid;

	pid = (mbox_id & sysvmq_inst.itccoord_mask) >> sysvmq_inst.itccoord_shift;
	if(pid == 0 || pid >= MAX_SUPPORTED_PROCESSES)
	{
		printf("\tDEBUG: get_sysvmq_cl - Invalid contact list's process id!\n");
		rc->flags |= ITC_INVALID_ARGUMENTS;
		return NULL;
	}

	return &(sysvmq_inst.sysvmq_cl[pid]);
}

static void add_sysvmq_cl(struct result_code* rc, struct sysvmq_contactlist* cl, itc_mbox_id_t mbox_id)
{
	int sysv_msqid;

	sysv_msqid = get_sysvmq_id(rc, mbox_id);
	if(sysv_msqid != -1)
	{
		cl->mbox_id_in_itccoord = (mbox_id & sysvmq_inst.itccoord_mask);
		cl->sysvmq_id = sysv_msqid;
	} else
	{
		printf("\tDEBUG: add_sysvmq_cl - sysv_msqid = -1!\n");
	}
}

static int get_sysvmq_id(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t new_mbx_id;
	int proj_id;
	key_t key;
	int msqid;

	new_mbx_id = mbox_id & sysvmq_inst.itccoord_mask;
	proj_id = (new_mbx_id >> sysvmq_inst.itccoord_shift);

	printf("\tDEBUG: get_sysvmq_id - proj_id = %d!\n", proj_id);
	key = ftok(ITC_SYSVMSQ_FILENAME, proj_id);

	if(key == -1)
	{
		perror("\tDEBUG: get_sysvmq_id - ftok");
		rc->flags |= ITC_SYSCALL_ERROR;
		// Don't need to return here?
	}

	msqid = msgget(key, 0); // Use 0 to get the previously created msqid or create a new one, just to avoid unecessary EEXIST

	if(msqid == -1)
	{
		perror("\tDEBUG: get_sysvmq_id - msgget");
		rc->flags |= ITC_SYSCALL_ERROR;
		if(errno == ENOENT)
		{
			return -1;
		}
	}

	return msqid;
}

static void remove_sysvmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	(void)rc;
	struct sysvmq_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	cl->mbox_id_in_itccoord = 0;
	cl->sysvmq_id = 0;
}

static void forward_sysvmq_msg(struct result_code* rc, char* buffer, int length, int msqid)
{
	(void)length;
	(void)msqid;
	(void)rc;

	struct itc_message* message;
	struct itc_message* rxmsg;
	union itc_msg* msg;
	uint16_t flags;

	rxmsg = (struct itc_message*)(buffer + 8);

#ifdef UNITTEST
	struct itc_message* tmp_message;
	tmp_message = (struct itc_message *)malloc(rxmsg->size + ITC_HEADER_SIZE + 1);
	tmp_message->flags = 0;
	msg = CONVERT_TO_MSG(tmp_message);
#else
	msg = itc_alloc(rxmsg->size, 0);
#endif

	message = CONVERT_TO_MESSAGE(msg);

	flags = message->flags; // Saved flags

	memcpy(message, rxmsg, (rxmsg->size + ITC_HEADER_SIZE));

	message->flags = flags; // Retored flags

#ifdef UNITTEST
	// Simulate that everything is ok at this point. Do nothing in unit test.
	// API itc_send is an external interface, so do not care about it if everything we pass into it is all correct.
	printf("\tDEBUG: forward_sysvmq_msg - ENTER UNITTEST!\n");
	free(tmp_message);
#endif

	printf("\tDEBUG: forward_sysvmq_msg - Forwarding a message to local mailbox from external mbox = 0x%08x\n", message->sender);
	itc_send(&msg, message->receiver, ITC_MY_MBOX_ID);
}

static void rxthread_destructor(void* data)
{
	(void)data;

	if(sysvmq_inst.my_sysvmq_id != -1)
	{
		sysvmq_inst.is_terminated = 1;
		if(msgctl(sysvmq_inst.my_sysvmq_id, IPC_RMID, NULL) == -1)
		{
			// ERROR trace is needed here
			perror("\tDEBUG: rxthread_destructor - msgctl");
		}
		sysvmq_inst.my_sysvmq_id = -1;
	}

	free(sysvmq_inst.rx_buffer);
}

