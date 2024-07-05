#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <search.h>
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/prctl.h>

#include <mqueue.h>
#include <sys/types.h>
#include <sys/stat.h>
// #include <linux/version.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                    INTERNAL TYPES IN SYSV-ATOR                       *****
*******************************************************************************/
#define MAX_NUM_MESSAGES_ALLOWED_IN_TREE	100

struct posixmq_contactlist {
	itc_mbox_id_t	mbox_id_in_itccoord;
	mqd_t		posix_mqd;
};

struct posixmq_instance {
	itc_mbox_id_t           	itccoord_mask;
        itc_mbox_id_t           	itccoord_shift;

	mqd_t				my_posix_mqd; // POSIX message queue descriptor
	char				my_posixmq_name[64];

	int				is_initialized;
	long				max_msgsize;
	char*				rx_buffer;

	pthread_mutex_t			itc_message_buffer_mtx;
	void				*itc_message_buffer_tree;
	unsigned int			num_messages;

	void				*m_active_mbox_tree;
	pthread_mutex_t			m_active_mbox_tree_mtx;

	struct posixmq_contactlist	posixmq_cl[MAX_SUPPORTED_PROCESSES];
};


/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct posixmq_instance posixmq_inst; // One instance per a process, multiple threads all use this one.



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_posixmq_resources(struct result_code* rc);
static void remove_posixmq();
static struct posixmq_contactlist* get_posixmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static struct posixmq_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static void add_posixmq_cl(struct result_code* rc, struct posixmq_contactlist* cl, itc_mbox_id_t mbox_id);
static mqd_t get_posix_mqd(struct result_code* rc, itc_mbox_id_t mbox_id);
// static void remove_posixmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static int compare_mboxid_in_itcmessage_tree(const void *pa, const void *pb);
static int compare_msg_in_itcmessage_tree(const void *pa, const void *pb);
static void notify_receiver_about_msg_arrival(struct itc_mailbox* mbox);
static int compare_mboxid_in_itcmailbox_tree(const void *pa, const void *pb);
static int compare_mbox_in_itcmailbox_tree(const void *pa, const void *pb);
static void do_nothing(void *tree_node_data);
static void posix_msq_rx_thread_func(union sigval sv);
static void register_notification(mqd_t *p_msqd);
static void free_itc_message_in_tree(void *tree_node_data);
// static void print_queue();
static void process_received_message(char *rx_buffer, ssize_t num);




/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void posixmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags);

static void posixmq_exit(struct result_code* rc);

static void posixmq_create_mbox(struct result_code* rc, struct itc_mailbox *mailbox, uint32_t flags);

static void posixmq_delete_mbox(struct result_code* rc, struct itc_mailbox *mailbox);

static void posixmq_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static struct itc_message *posixmq_receive(struct result_code* rc, struct itc_mailbox *my_mbox);

static long posixmq_maxmsgsize(struct result_code* rc);

struct itci_transport_apis posixmq_trans_apis = { NULL,
                                            	posixmq_init,
                                            	posixmq_exit,
                                            	posixmq_create_mbox,
                                            	posixmq_delete_mbox,
                                            	posixmq_send,
                                            	posixmq_receive,
                                            	NULL,
                                            	posixmq_maxmsgsize };





/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
void posixmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;

	if(posixmq_inst.is_initialized == 1)
	{
		if(flags & ITC_FLAGS_FORCE_REINIT)
		{
			TPT_TRACE(TRACE_INFO, "Force re-initializing!");
			release_posixmq_resources(rc);
			if(rc != ITC_OK)
			{
				TPT_TRACE(TRACE_ERROR, "Failed to release_posixmq_resources!");
				return;
			}
		} else
		{
			TPT_TRACE(TRACE_INFO, "Already initialized!");
			rc->flags |= ITC_ALREADY_INIT;
			return;
		}
	}

	posixmq_inst.num_messages = 0;

	// Calculate itccoord_shift value
	int tmp_shift, tmp_mask;
	tmp_shift = 0;
	tmp_mask = itccoord_mask;
	while(!(tmp_mask & 0x1))
	{
		tmp_mask = tmp_mask >> 1;
		tmp_shift++; // Should be 20 for current design
	}

	posixmq_inst.itccoord_mask 	= itccoord_mask;
	posixmq_inst.itccoord_shift	= tmp_shift;

	sprintf(posixmq_inst.my_posixmq_name, "/itc_posixmq_0x%08x", my_mbox_id_in_itccoord);

	char posixmq_path[128];
	sprintf(posixmq_path, "/dev/mqueue%s", posixmq_inst.my_posixmq_name);

	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 10; // set max number of msg in queue to 10 as default
	attr.mq_msgsize = posixmq_maxmsgsize(rc);
	attr.mq_curmsgs = 0;

	if ((posixmq_inst.my_posix_mqd = mq_open(posixmq_inst.my_posixmq_name, O_RDONLY | O_CREAT | O_CLOEXEC | O_NONBLOCK, 0666, &attr)) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to mq_open, errno = %d!", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	int res = chmod(posixmq_path, 0777);
	if(res < 0)
	{
		/*
		If the errno = 2 even though mq_open has been successful, you must mount your filesystem /dev/mqueue onto type mqueue
		by doing this command under root privilege:
			>> sudo mount -t mqueue none /dev/mqueue
		*/
		TPT_TRACE(TRACE_ERROR, "Failed to chmod, file = %s, res = %d, errno = %d!", posixmq_path, res, errno);
		remove_posixmq();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	posixmq_inst.rx_buffer = malloc(posixmq_inst.max_msgsize);
	if(posixmq_inst.rx_buffer == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to malloc posixmq_inst.rx_buffer!");
		remove_posixmq();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = pthread_mutex_init(&posixmq_inst.itc_message_buffer_mtx, NULL);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_mutex_init, error code = %d", res);
		remove_posixmq();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
	
	res = pthread_mutex_init(&posixmq_inst.m_active_mbox_tree_mtx, NULL);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_mutex_init, error code = %d", res);
		remove_posixmq();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	register_notification(&posixmq_inst.my_posix_mqd);

	posixmq_inst.is_initialized = 1;
}

static void posixmq_exit(struct result_code* rc)
{
	if(!posixmq_inst.is_initialized)
	{
		TPT_TRACE(TRACE_ABN, "Not initialized yet!");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	release_posixmq_resources(rc);
}

static void posixmq_create_mbox(struct result_code* rc, struct itc_mailbox *mailbox, uint32_t flags)
{
	(void)rc;
	(void)flags;

	MUTEX_LOCK(&posixmq_inst.m_active_mbox_tree_mtx);
	struct itc_mailbox **iter = NULL;
	iter = tfind(&mailbox->mbox_id, &posixmq_inst.m_active_mbox_tree, compare_mboxid_in_itcmailbox_tree);
	if(iter != NULL)
	{
		TPT_TRACE(TRACE_ABN, "Mailbox \"%s\" 0x%08x already exists in tree, something abnormal!", mailbox->name, mailbox->mbox_id);
		return;
	}

	tsearch(mailbox, &posixmq_inst.m_active_mbox_tree, compare_mbox_in_itcmailbox_tree);
	MUTEX_UNLOCK(&posixmq_inst.m_active_mbox_tree_mtx);

	TPT_TRACE(TRACE_INFO, "Adding mailbox \"%s\" 0x%08x into posixmq_tree!", mailbox->name, mailbox->mbox_id);
}

static void posixmq_delete_mbox(struct result_code* rc, struct itc_mailbox *mailbox)
{
	(void)rc;

	MUTEX_LOCK(&posixmq_inst.m_active_mbox_tree_mtx);
	struct itc_mailbox **iter = NULL;
	iter = tfind(&mailbox->mbox_id, &posixmq_inst.m_active_mbox_tree, compare_mboxid_in_itcmailbox_tree);
	if(iter == NULL)
	{
		TPT_TRACE(TRACE_ABN, "Mailbox \"%s\" 0x%08x not found in tree, something abnormal!", mailbox->name, mailbox->mbox_id);
		return;
	}

	tdelete(mailbox, &posixmq_inst.m_active_mbox_tree, compare_mbox_in_itcmailbox_tree);
	MUTEX_UNLOCK(&posixmq_inst.m_active_mbox_tree_mtx);
}

static void posixmq_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to)
{
	struct posixmq_contactlist* cl;

	if(!posixmq_inst.is_initialized)
	{
		TPT_TRACE(TRACE_ABN, "Not initialized yet!");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	cl = get_posixmq_cl(rc, to);
	if(cl == NULL || cl->mbox_id_in_itccoord == 0)
	{
		TPT_TRACE(TRACE_ABN, "Receiver side not initialised message queue yet!");
		rc->flags &= ~ITC_SYSCALL_ERROR; // The receiver side has not initialised message queue yet
		rc->flags |= ITC_QUEUE_NULL; // So remove unecessary syscall error, return an ITC_QUEUE_NULL warning instead. This is not an ERROR at all!
		/* If send failed, users have to self-free the message. ITC system only free messages when send successfully */
		return;
	}

	int num_retries = 100;
	while(mq_send(cl->posix_mqd, (const char *)message, message->size + ITC_HEADER_SIZE + 1, 0) == -1) // Will send ENDPOINT as well for sanity check on receiver side
	{
		if(errno == EINTR || errno == EAGAIN || num_retries > 0)
		{
			/* The receiver side's message queue has been full and O_NONBLOC flag is set or the call mq_send was just interrupted
			by an incoming signal to our process, let's do some retries */
			--num_retries;
			continue;
		} else
		{
			// ERROR trace is needed here
			TPT_TRACE(TRACE_ERROR, "Failed to msgsnd(), num_retries = %d, errno = %d", num_retries, errno);
			rc->flags |= ITC_SYSCALL_ERROR; // Will not return here
		}
	}

	union itc_msg* msg;
#ifdef UNITTEST
	free(message);
	(void)msg; // Avoid gcc compiler warning unused of msg in UNITTEST scenario.
#else
	msg = CONVERT_TO_MSG(message);
	itc_free(&msg);
#endif
}

static struct itc_message *posixmq_receive(struct result_code* rc, struct itc_mailbox *my_mbox)
{
	(void)rc;
	struct itc_message* ret_message;
	struct itc_message** iter = NULL;

	MUTEX_LOCK(&posixmq_inst.itc_message_buffer_mtx);
	iter = tfind(&(my_mbox->mbox_id), &posixmq_inst.itc_message_buffer_tree, compare_mboxid_in_itcmessage_tree);
	if(iter != NULL)
	{
		ret_message = (struct itc_message*)(*iter);
		--posixmq_inst.num_messages;
		tdelete(&(my_mbox->mbox_id), &posixmq_inst.itc_message_buffer_tree, compare_mboxid_in_itcmessage_tree);
		// TPT_TRACE(TRACE_INFO, "Found itc message in POSIX message queue's buffer!"); // TBD
		MUTEX_UNLOCK(&posixmq_inst.itc_message_buffer_mtx);
		return ret_message;
	}
	MUTEX_UNLOCK(&posixmq_inst.itc_message_buffer_mtx);

	return NULL;
}

static long posixmq_maxmsgsize(struct result_code* rc)
{
	(void)rc;
	struct mq_attr attr;

	if(posixmq_inst.my_posix_mqd != -1 && mq_getattr(posixmq_inst.my_posix_mqd, &attr) != -1)
	{
		posixmq_inst.max_msgsize = attr.mq_msgsize;
	} else
	{
		/* Below implementation only accepted after either:
		>> sudo sysctl -w fs.mqueue.msgsize_max=16777216
		or add this line into /etc/sysctl.conf
		"fs.mqueue.msgsize_max=16777216"
		and run:
		>> sudo sysctl -p
		*/

		// int maj = LINUX_VERSION_CODE >> 16;
		// int min = (LINUX_VERSION_CODE - (maj << 16)) >> 8;
		// int pat = LINUX_VERSION_CODE - (maj << 16) - (min << 8);

		// // Taken from https://man7.org/linux/man-pages/man7/mq_overview.7.html
		// if(maj <= 2 && min <= 6 && pat <= 28)
		// {
		// 	// Before Linux kernel 2.6.28
		// 	// posixmq_inst.max_msgsize = INT_MAX;
		// 	posixmq_inst.max_msgsize = 32767; // 32KB
		// } else if(maj <= 3 && min <= 4)
		// {
		// 	// From Linux kernel 2.6.28 to 3.4
		// 	posixmq_inst.max_msgsize = 1048576; // 1MB
		// {
		// 	// Since Linux kernel 3.5
		// 	posixmq_inst.max_msgsize = 16777216; // 16MB
		// }

		posixmq_inst.max_msgsize = 8192; // Taken from /proc/sys/fs/mqueue/msgsize_default
	}

	TPT_TRACE(TRACE_INFO, "Retrieve POSIX msg queue's max message size successfully, %lu bytes!", posixmq_inst.max_msgsize);
	return posixmq_inst.max_msgsize;
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_posixmq_resources(struct result_code* rc)
{
	(void)rc;
	if(posixmq_inst.my_posix_mqd != -1)
	{
		remove_posixmq();
	}

	if(posixmq_inst.rx_buffer != NULL)
	{
		free(posixmq_inst.rx_buffer);
	}

	int ret = pthread_mutex_destroy(&posixmq_inst.itc_message_buffer_mtx);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "pthread_mutex_destroy error code = %d", ret);
	}
	
	ret = pthread_mutex_destroy(&posixmq_inst.m_active_mbox_tree_mtx);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "pthread_mutex_destroy error code = %d", ret);
	}

	tdestroy(posixmq_inst.itc_message_buffer_tree, free_itc_message_in_tree);
	tdestroy(posixmq_inst.m_active_mbox_tree, do_nothing);

	posixmq_inst.is_initialized = -1;
	memset(&posixmq_inst, 0, sizeof(struct posixmq_instance));
}

static void remove_posixmq()
{
	if (mq_close(posixmq_inst.my_posix_mqd) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to mq_close, my_posix_mqd = %d, errno = %d!", posixmq_inst.my_posix_mqd, errno);
	}

	if (mq_unlink(posixmq_inst.my_posixmq_name) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to mq_unlink, my_posixmq_name = %s, errno = %d!", posixmq_inst.my_posixmq_name, errno);
	}
}

static struct posixmq_contactlist* get_posixmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	struct posixmq_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	if(cl == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Contact list not found!");
		return NULL;
	}

	if(cl->mbox_id_in_itccoord == 0)
	{
		// TPT_TRACE(TRACE_INFO, "Add contact list!"); // TBD
		add_posixmq_cl(rc, cl, mbox_id);
	}

	return cl;
}

static struct posixmq_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t pid;

	pid = (mbox_id & posixmq_inst.itccoord_mask) >> posixmq_inst.itccoord_shift;
	if(pid == 0 || pid >= MAX_SUPPORTED_PROCESSES)
	{
		TPT_TRACE(TRACE_ABN, "Invalid contact list's process id!");
		rc->flags |= ITC_INVALID_ARGUMENTS;
		return NULL;
	}

	return &(posixmq_inst.posixmq_cl[pid]);
}

static void add_posixmq_cl(struct result_code* rc, struct posixmq_contactlist* cl, itc_mbox_id_t mbox_id)
{
	mqd_t posix_msqd;

	posix_msqd = get_posix_mqd(rc, mbox_id);
	if(posix_msqd != -1)
	{
		cl->mbox_id_in_itccoord = (mbox_id & posixmq_inst.itccoord_mask);
		cl->posix_mqd = posix_msqd;
	} else
	{
		TPT_TRACE(TRACE_ERROR, "posix_msqd = -1!");
	}
}

static mqd_t get_posix_mqd(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t partner_mboxid_in_itccoord;
	char partner_name[64];
	mqd_t msqd = -1;

	partner_mboxid_in_itccoord = mbox_id & posixmq_inst.itccoord_mask;
	sprintf(partner_name, "/itc_posixmq_0x%08x", partner_mboxid_in_itccoord);

	if ((msqd = mq_open(partner_name, O_WRONLY)) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to mq_open partner posix msgqueue %s, errno = %d!", partner_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return -1;
	}

	// TPT_TRACE(TRACE_INFO, "Get posix message queue descriptor %s successfully, msqd = %d!", partner_name, msqd); // TBD
	return msqd;
}

// static void remove_posixmq_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
// {
// 	(void)rc;
// 	struct posixmq_contactlist* cl;

// 	cl = find_cl(rc, mbox_id);
// 	cl->mbox_id_in_itccoord = 0;
// 	cl->posix_mqd = 0;
// }

static void free_itc_message_in_tree(void *tree_node_data)
{
	struct itc_message *message = *((struct itc_message **)tree_node_data);

#ifdef UNITTEST
	free(message);
#else
	union itc_msg *msg = CONVERT_TO_MSG(message);
	itc_free(&msg);
#endif

	
}

static int compare_mboxid_in_itcmessage_tree(const void *pa, const void *pb)
{
	const itc_mbox_id_t *mboxid = pa;
	const struct itc_message *message = pb;

	if(*mboxid == message->receiver)
	{
		return 0;
	} else if(*mboxid > message->receiver)
	{
		return 1;
	} else
	{
		return -1;
	}
}

static int compare_msg_in_itcmessage_tree(const void *pa, const void *pb)
{
	if((unsigned long)pa == (unsigned long)pb)
	{
		return 0;
	} else if((unsigned long)pa > (unsigned long)pb)
	{
		return 1;
	} else
	{
		return -1;
	}
}

static void notify_receiver_about_msg_arrival(struct itc_mailbox* mbox)
{
	MUTEX_LOCK(&(mbox->rxq_info.rxq_mtx));
	int saved_cancel_state;

	/* System call write() below will create a cancellation point that can cause this thread get cancelled unexpectedly */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &saved_cancel_state);

	uint64_t one = 1;
	if(mbox->p_rxq_info->is_fd_created && mbox->p_rxq_info->rxq_len == 0)
	{
		if(write(mbox->p_rxq_info->rxq_fd, &one, 8) < 0)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to write()!");
		}
	}

	mbox->p_rxq_info->rxq_len++;
	pthread_cond_signal(&(mbox->p_rxq_info->rxq_cond));
	MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));

	pthread_setcancelstate(saved_cancel_state, NULL);
	// TPT_TRACE(TRACE_INFO, "Notify mailbox receiver 0x%08x about an incoming messages!", mbox->mbox_id); // TBD
}

static int compare_mboxid_in_itcmailbox_tree(const void *pa, const void *pb)
{
	const itc_mbox_id_t *mboxid = pa;
	const struct itc_mailbox *mbox = pb;

	if(*mboxid == mbox->mbox_id)
	{
		return 0;
	} else if(*mboxid > mbox->mbox_id)
	{
		return 1;
	} else
	{
		return -1;
	}
}

static int compare_mbox_in_itcmailbox_tree(const void *pa, const void *pb)
{
	const struct itc_mailbox *mbox1 = pa;
	const struct itc_mailbox *mbox2 = pb;

	if(mbox1->mbox_id == mbox2->mbox_id)
	{
		return 0;
	} else if(mbox1->mbox_id > mbox2->mbox_id)
	{
		return 1;
	} else
	{
		return -1;
	}
}

static void do_nothing(void *tree_node_data)
{
	(void)tree_node_data;
}

static void posix_msq_rx_thread_func(union sigval sv)
{
	ssize_t numRead;
	mqd_t *mqdp = NULL;

	/* Optimize send-receive latency */
	// char tn[20];
	// sprintf(tn, "POSIXMQ(%ld)", syscall(SYS_gettid));
	// prctl(PR_SET_NAME, tn, 0, 0, 0);

	// TPT_TRACE(TRACE_INFO, "START: New thread to receive POSIX message!"); // TBD

	mqdp = sv.sival_ptr;
	int num_retries = 100;
	while (1)
	{
		// print_queue();
		numRead = mq_receive(*mqdp, posixmq_inst.rx_buffer, posixmq_inst.max_msgsize, NULL);		

		if(numRead < 0)
		{
			if(errno == EINTR || num_retries > 0)
			{
				/* The call mq_receive was just interrupted
				by an incoming signal to our process during reading received bytes, let's do some retries */
				--num_retries;
				continue;
			} else if(errno == EAGAIN)
			{
				/* Non blocking mode, EAGAIN returned if msg queue was just empty */
				register_notification(mqdp);
				/* DO NOT perform any extra actions here to prevent a situation,
				where there's probably a new message coming into the queue at this point, then it will be never processed then */
				return;
				// print_queue();
				// TPT_TRACE(TRACE_ABN, "POSIX message queue has been emptied!");
				// break;
			} else
			{
				// ERROR trace is needed here
				TPT_TRACE(TRACE_ERROR, "Failed to msgsnd(), num_retries = %d, errno = %d", num_retries, errno);
				break;
			}
		} else
		{
			process_received_message(posixmq_inst.rx_buffer, numRead);
		}
	}
}

static void register_notification(mqd_t *p_msqd)
{
	struct sigevent sev;
	sev.sigev_value.sival_ptr = p_msqd;
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = posix_msq_rx_thread_func;
	sev.sigev_notify_attributes = NULL;

	if(mq_notify(*p_msqd, &sev) == -1)
 	{
		TPT_TRACE(TRACE_ERROR, "Failed to mq_notify(), errno = %d", errno);
	}
}


// static void print_queue()
// {
// 	FILE *fptr;
// 	fptr = fopen("/dev/mqueue/itc_rx_posixmq_0x00100000", "r"); 
// 	if (fptr == NULL)
// 	{
// 		TPT_TRACE(TRACE_ERROR, "Cannot open file!\n");
// 		return;
// 	}

// 	char buff[256];

// 	char c = fgetc(fptr);
// 	int i = 0;
// 	while (c != EOF)
// 	{
// 		buff[i] = c;
// 		c = fgetc(fptr);
// 		++i;
// 	}

// 	buff[i] = '\0';
// 	TPT_TRACE(TRACE_INFO, "itccoord POSIX mqueue: %s\n", buff);

// 	fclose(fptr);
// }

static void process_received_message(char *rx_buffer, ssize_t num)
{
	if(num <= ITC_HEADER_SIZE)
	{
		TPT_TRACE(TRACE_ABN, "Received malform message from some mailbox, msg size too small (%ld)!", num);
		return;
	} else
	{
		// TPT_TRACE(TRACE_INFO, "Received %ld bytes from mq_receive()!", num); // TBD
	}

	// print_queue();
	struct itc_message* message;
	struct itc_message* rxmsg;
	union itc_msg* msg;
	uint16_t flags;

	rxmsg = (struct itc_message *)rx_buffer;

	char *endpoint = (char*)((unsigned long)(&rxmsg->msgno) + rxmsg->size);
	if(*endpoint != ENDPOINT)
	{
		TPT_TRACE(TRACE_ABN, "Received malform message from some mailbox, invalid ENDPOINT 0x%02x!", *endpoint & 0xFF);
		return;
	}

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
	memcpy(message, rxmsg, (rxmsg->size + ITC_HEADER_SIZE + 1));
	message->flags = flags; // Retored flags

	if(posixmq_inst.num_messages > MAX_NUM_MESSAGES_ALLOWED_IN_TREE)
	{
		TPT_TRACE(TRACE_ERROR, "POSIX MQ message tree-buffer overflow with %u un-handled itc messages!", posixmq_inst.num_messages);
#ifdef UNITTEST
		free(message);
		message = NULL;
#else
		itc_free(&msg);
#endif
		return;
	}

	/* First save the incoming message into our tree buffer */
	MUTEX_LOCK(&posixmq_inst.itc_message_buffer_mtx);
	// TPT_TRACE(TRACE_INFO, "Inserting this itc message into tree-buffer, msgno = 0x%08x, receiver = 0x%08x", message->msgno, message->receiver); // TBD
	tsearch(message, &posixmq_inst.itc_message_buffer_tree, compare_msg_in_itcmessage_tree);
	++posixmq_inst.num_messages;
	MUTEX_UNLOCK(&posixmq_inst.itc_message_buffer_mtx);

	/* Try to find if any active mbox in this process matches receiver field of the incoming message 
	If yes, notify them to pick up the message
	If they are active but did not pick up the message, so highly possible that receiver not waiting on either
	itc_receive() or monitor on mbox_fd using epoll/poll/select */
	MUTEX_LOCK(&posixmq_inst.m_active_mbox_tree_mtx);
	struct itc_mailbox **mbox_iter = NULL;
	mbox_iter = tfind(&message->receiver, &posixmq_inst.m_active_mbox_tree, compare_mboxid_in_itcmailbox_tree);
	if(mbox_iter == NULL)
	{
		/* But still store the message in the msg buffer tree if some one want to pick up it later.
		If still no one does, drop it when newer coming message with same receiver field */
		TPT_TRACE(TRACE_ABN, "No active mailbox matched incoming message's receiver = 0x%08x", message->receiver);
		MUTEX_UNLOCK(&posixmq_inst.m_active_mbox_tree_mtx);
		return;
	}

	notify_receiver_about_msg_arrival(*mbox_iter);
	MUTEX_UNLOCK(&posixmq_inst.m_active_mbox_tree_mtx);
}









