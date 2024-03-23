/* Until now, everything, at least local/sysvmq/lsock transportation, is ready. 
* So will moving on to itc.c implementation - the main function of ITC system */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <search.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_alloc.h"
#include "itci_trans.h"
#include "itc_threadmanager.h"
#include "itc_proto.h"
#include "itc_queue.h"



/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
union itc_msg {
	uint32_t				msgno;

	struct itc_notify_coord_add_rmv_mbox	itc_notify_coord_add_rmv_mbox;
};

struct itc_instance {
	struct itc_queue*		free_mboxes_queue; // a queue of free/has-been-deleted mailboxes that can be re-used later

	itc_mbox_id_t			my_mbox_id_in_itccoord; // mailbox id of this process
	itc_mbox_id_t			itccoord_mask; // mask for itccoord process id, should be 0xFFF0 0000
	itc_mbox_id_t			itccoord_mbox_id;

	struct itc_threads*		thread_list;	// manage a list of threads that is started by itc.c via itc_init() such as sysvmq_rx_thread,...
	pthread_mutex_t			thread_list_mtx;

	pthread_key_t			destruct_key;

	pid_t				pid;

	uint32_t			nr_mboxes;
	uint32_t			local_mbox_mask; // mask for local mailbox id
	struct itc_mailbox*		mboxes; // List of mailboxes allocated by malloc	
};

/*****************************************************************************\/
*****                     INTERNAL VARIABLES IN ITC.C                      *****
*******************************************************************************/
struct itc_instance	itc_inst;
static struct itci_transport_apis trans_mechanisms[ITC_NUM_TRANS];
static struct itci_alloc_apis	alloc_mechanisms;

/* When a thread requests for creating a mailbox, there is a itc_mailbox pointer to their mailbox and only it owns its pointer */
static __thread struct itc_mailbox*	my_threadlocal_mbox[ITC_MAX_MAILBOXES_PER_THREAD] = NULL;
static __thread uint32_t		my_threadlocal_nr_mboxes = 0;

extern struct itci_alloc_apis local_trans_apis;
extern struct itci_alloc_apis sysvmq_trans_apis;
extern struct itci_alloc_apis lsock_trans_apis;

extern struct itci_alloc_apis malloc_apis;

/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_all_itc_resources(void);
static void mailbox_destructor_at_thread_exit(void* data);
static struct itc_mailbox* find_mbox(itc_mbox_id_t mbox_id);
static void calc_time(struct timespec* ts, unsigned long tmo);


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
bool itc_init_zz(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags)
{
	(void)namespace; // Will be implemented later

	int max_msgsize = ITC_MAX_MSGSIZE;
	uint32_t flags = 0;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	/* Re-init */
	if(itc_inst.mboxes != NULL)
	{
		if(getpid() == itc_inst.pid)
		{
			/* Already initialized */
			free(rc);
			return false;
		} else
		{
			if(alloc_mechanisms.itci_alloc_exit != NULL)
				{
					alloc_mechanisms.itci_alloc_exit(rc);
				}
			release_all_itc_resources();
			flags = ITC_FLAGS_FORCE_REINIT;
		}
	}

	itc_inst.pid = getpid();

	if(pthread_mutex_init(&itc_inst.thread_list_mtx, NULL) != 0)
	{
		free(rc);
		return false;
	}

	nr_mboxes += 1; // We will need 1 extra mailboxes for sysvmq

	trans_mechanisms[ITC_TRANS_LOCAL]	= local_trans_apis;
	trans_mechanisms[ITC_TRANS_SYSVMQ]	= sysvmq_trans_apis;
	trans_mechanisms[ITC_TRANS_LSOCK]	= lsock_trans_apis;

	if(alloc_scheme == ITC_MALLOC)
	{
		alloc_mechanisms = malloc_apis;
	} // else if ... for memory pooling,...

	for(int i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_maxmsgsize != NULL)
		{
			int msgsize = 0;

			msgsize = trans_mechanisms[i].itci_trans_maxmsgsize(rc);
			if(msgsize > max_msgsize)
			{
				max_msgsize = msgsize;
			}
			rc->flags = ITC_OK;
		}
	}

	if(alloc_mechanisms.itci_alloc_init != NULL)
	{
		alloc_mechanisms.itci_alloc_init(rc, max_msgsize);
		rc->flags = ITC_OK;
	}

	if(init_flags & ITC_FLAGS_I_AM_ITC_COORD)
	{
		itc_inst.itccoord_mask = ITC_COORD_MASK;
		itc_inst.itccoord_mbox_id = 1 << ITC_COORD_SHIFT;
		itc_inst.my_mbox_id_in_itccoord = (1 << ITC_COORD_SHIFT) | 1;
	} else
	{
		int i = 0;
		for(; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_locate_itccoord != NULL && \
			trans_mechanisms[i].itci_trans_locate_itccoord(rc, &itc_inst.my_mbox_id_in_itccoord, &itc_inst.itccoord_mask, &itc_inst.itccoord_mbox_id))
			{
				break;
			}
		}

		/* Failed to locate itccoord which is not acceptable! */
		if(i == ITC_NUM_TRANS)
		{
			free(rc);
			return false;
		}
	}

	itc_inst.mboxes = (struct itc_mailbox*)malloc(nr_mboxes*sizeof(itc_mailbox));
	if(itc_inst.mboxes == NULL)
	{
		free(rc);
		return false;
	}
	memset(itc_inst.mboxes, 0, nr_mboxes*sizeof(itc_mailbox));

	itc_inst.local_mbox_mask = 0xFFFFFFFF >> CLZ(nr_mboxes);
	itc_inst.nr_mboxes = nr_mboxes;

	struct itc_mailbox* mbox_iter;
	pthread_condattr_t condattr;
	for(int i=0; i < itc_inst.nr_mboxes; i++)
	{
		mbox_iter = itc_inst.mboxes[i];
		mbox_iter->mbox_id = itc_inst.my_mbox_id_in_itccoord | i;

		if(pthread_mutexattr_init(&(mbox_iter->rxq_info.rxq_attr)) != 0)
		{
			perror("pthread_mutexattr_init");
			free(rc);
			return false;
		}

		/* Use this type of mutex is safetest, check man7 page for details */
		if(pthread_mutexattr_settype(&(mbox_iter->rxq_info.rxq_attr), PTHREAD_MUTEX_ERRORCHECK) != 0)
		{
			perror("pthread_mutexattr_settype");
			free(rc);
			return false;
		}

		if(pthread_mutex_init(&(mbox_iter->rxq_info.rxq_mtx), &(mbox_iter->rxq_info.rxq_attr)) != 0)
		{
			perror("pthread_mutex_init");
			free(rc);
			return false;
		}

		if(pthread_condattr_init(&condattr) != 0)
		{
			perror("pthread_condattr_init");
			free(rc);
			return false;
		}

		/* Use clock that indicates the period of time in second from when the system is booted to measure time serving for pthread_cond_timedwait */
		if(pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0)
		{
			perror("pthread_condattr_setclock");
			free(rc);
			return false;
		}

		if(pthread_cond_init(&(mbox_iter->rxq_info.rxq_cond), &condattr) != 0)
		{
			perror("pthread_cond_init");
			free(rc);
			return false;
		}
		
		if(i == 0)
		{
			itc_inst.free_mboxes_queue = q_init();
		}
		q_enqueue(rc, itc_inst.free_mboxes_queue, mbox_iter);
	}

	if(pthread_key_create(&itc_inst.destruct_key, mailbox_destructor_at_thread_exit) != 0)
	{
		perror("pthread_key_create");
		free(rc);
		return false;
	}

	for(int i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_init != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_init(rc, itc_inst.my_mbox_id_in_itccoord, \
						itc_inst.itccoord_mask, itc_inst.nr_mboxes, flags);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				free(rc);
				return false;
			}
		}
	}

	start_itcthreads(rc); // Start sysvmq_rx_thread
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		free(rc);
		return false;
	}

	free(rc);
	return true;
}

bool itc_exit_zz()
{
	struct itc_mailbox* mbox;
	int running_mboxes = 0;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	/* If itc_inst.mboxes == NULL, only two possible cases. One is mutex_init failed, or failed to locate itccoord */
	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		if(itc_inst.thread_list_mtx != NULL)
		{
			pthread_mutex_destroy(&itc_inst.thread_list_mtx);
		}
		free(rc);
		return false;
	}

	for(int i = 0; i < itc_inst.nr_mboxes; i++)
	{
		mbox = itc_inst.mboxes[i];
		
		MUTEX_LOCK(rc, &mbox->rxq_info.rxq_mtx);

		if(mbox->mbox_state == MBOX_INUSE)
		{
			running_mboxes++;
		}

		MUTEX_UNLOCK(rc, &mbox->rxq_info.rxq_mtx);

		if(running_mboxes > ITC_NR_INTERNAL_USED_MBOXES)
		{
			// ERROR trace is needed here
			free(rc);
			return false;
		}
	}

	rc->flags = ITC_OK;
	terminate_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		free(rc);
		return false;
	}

	for(int i = 0; i < itc_inst.nr_mboxes; i++)
	{
		mbox = itc_inst.mboxes[i];

		if(pthread_cond_destroy(&(mbox->rxq_info.rxq_cond)) != 0)
		{
			// ERROR trace is needed here
			free(rc);
			return false;
		}

		if(pthread_mutex_destroy(&(mbox->rxq_info.rxq_mtx)) != o)
		{
			// ERROR trace is needed here
			free(rc);
			return false;
		}

		if(pthread_mutexattr_destroy(&(mbox->rxq_info.rxq_attr)) != 0)
		{
			// ERROR trace is needed here
			free(rc);
			return false;
		}
	}

	for(int i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_exit != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_exit(rc);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				free(rc);
				return false;
			}
		}
	}

	if(pthread_key_delete(itc_inst.destruct_key) != 0)
	{
		// ERROR trace is needed here
		free(rc);
		return false;
	}

	if(alloc_mechanisms.itci_alloc_exit != NULL)
	{
		alloc_mechanisms.itci_alloc_exit(rc);
	}

	free(itc_inst.mboxes);

	rc->flags = ITC_OK;
	q_exit(rc, itc_inst.free_mboxes_queue);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		free(rc);
		return false;
	}

	free(rc);
	return true;
}

union itc_msg *itc_alloc_zz(size_t size, uint32_t msgno)
{
	struct itc_message* message;
	char* endpoint;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return NULL;
	}

	if(size < sizeof(msgno))
	{
		size = sizeof(msgno);
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return NULL;
	}

	message = alloc_mechanisms.itci_alloc_alloc(size + ITC_HEADER_SIZE + 1);
	if(message == NULL)
	{
		free(rc);
		return NULL;
	}

	message->msgno = msgno;
	message->sender = 0;
	message->receiver = 0;
	message->size = size;
	message->flags = 0;
	endpoint = (char*)(unsigned long)(&message->msgno + size);
	*endpoint = ENDPOINT;

	free(rc);
	return (union itc_msg*)&(message->msgno);
}

bool itc_free_zz(union itc_msg **msg)
{
	struct itc_message* message;
	char* endpoint;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		free(rc);
		return false;
	}

	message = CONVERT_TO_MESSAGE(*msg);
	endpoint = (char*)(unsigned long)(&message->msgno + message->size);

	if(message->flags & ITC_FLAGS_MSG_INRXQUEUE)
	{
		free(rc);
		return false;
	} else if(*endpoint != ENDPOINT)
	{
		free(rc);
		return false;
	}

	rc->flags = ITC_OK;
	alloc_mechanisms.itci_alloc_free(rc, &message);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		free(rc);
		return false;
	}

	*msg = NULL;
	free(rc);
	return true;
}

itc_mbox_id_t itc_create_mailbox_zz(const char *name, uint32_t flags)
{
	struct itc_mailbox* new_mbox;
	union itc_msg* msg;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return ITC_NO_MBOX_ID;
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	new_mbox = q_dequeue(itc_inst.free_mboxes_queue);
	if(new_mbox == NULL)
	{
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	if(strlen(name) > (ITC_MAX_MBOX_NAME_LENGTH))
	{
		free(rc);
		return ITC_NO_MBOX_ID;
	}
	strcpy(new_mbox->name, name);

	new_mbox->flags			= flags;
	new_mbox->p_rxq_info		= &new_mbox->rxq_info;
	new_mbox->p_rxq_info->rxq_len	= 0;
	new_mbox->p_rxq_info->is_in_rx	= 0;

	MUTEX_LOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));

	new_mbox->mbox_state		= MBOX_INUSE;
	new_mbox->tid			= (pid_t)syscall(SYS_gettid);

	for(int i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_create_mbox != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_create_mbox(rc, new_mbox, flags);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));
				free(rc);
				return ITC_NO_MBOX_ID;
			}
		}
	}

	if(pthread_setspecific(itc_inst.destruct_key, new_mbox) != 0)
	{
		// ERROR trace is needed here
		MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	my_threadlocal_mbox[my_threadlocal_nr_mboxes] = new_mbox;
	my_threadlocal_nr_mboxes++;

	MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));

	/* In case this process is not the itccoord process, send notification to itccoord */
	if(itc_inst.my_mbox_id_in_itccoord != (itc_inst.itccoord_mbox_id & itc_inst.itccoord_mask))
	{
		msg = itc_alloc(offsetof(struct itc_notify_coord_add_rmv_mbox, mbox_name) + strlen(name) + 1, ITC_NOTIFY_COORD_ADD_MBOX);
		msg->itc_notify_coord_add_rmv_mbox.mbox_id = new_mbox->mbox_id;
		strcpy(msg->itc_notify_coord_add_rmv_mbox.mbox_name, name);
		itc_send(&msg, itc_inst.itccoord_mbox_id, new_mbox->mbox_id);
	}

	free(rc);
	return new_mbox->mbox_id;
}

bool itc_delete_mailbox_zz(itc_mbox_id_t mbox_id)
{
	struct itc_mailbox* mbox;
	union itc_msg* msg;
	pthread_mutex_t* rxq_mtx;
	mbox_state_e mbox_state;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_nr_mboxes == 0)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return false;
	}

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		if(mbox_id == my_threadlocal_mbox[i]->mbox_id)
		{
			mbox = my_threadlocal_mbox[i];
			
			for(int j = i; j < my_threadlocal_nr_mboxes; j++)
			{
				my_threadlocal_mbox[j] = my_threadlocal_mbox[j+1];
			}
			my_threadlocal_nr_mboxes--;

			break;
		}
	}

	if(mbox == NULL)
	{
		// Not allowed to delete a mailbox of other threads
		free(rc);
		return false;
	}

	rxq_mtx = &(mbox->p_rxq_info->rxq_mtx);
	int res = pthread_mutex_lock(rxq_mtx);
	if(res != 0 && res != EDEADLK)
	{
		free(rc);
		return false;
	}

	mbox_state = mbox->mbox_state;

	mbox->mbox_state = MBOX_UNUSED;

	MUTEX_UNLOCK(rc, rxq_mtx);

	if(mbox->p_rxq_info->is_fd_created)
	{
		if(close(mbox->p_rxq_info->rxq_fd) == -1)
		{
			// ERROR trace is needed here
		}
		mbox->p_rxq_info->is_fd_created = false;
	}

	MUTEX_LOCK(rc, rxq_mtx);

	/* Notify itccoord of my deleted mailbox */
	if(itc_inst.my_mbox_id_in_itccoord != (itc_inst.itccoord_mbox_id & itc_inst.itccoord_mask))
	{
		msg = itc_alloc(offsetof(struct itc_notify_coord_add_rmv_mbox, mbox_name) + strlen(mbox->name) + 1, ITC_NOTIFY_COORD_RMV_MBOX);
		msg->itc_notify_coord_add_rmv_mbox.mbox_id = mbox->mbox_id;
		strcpy(msg->itc_notify_coord_add_rmv_mbox.mbox_name, mbox->name);
		itc_send(&msg, itc_inst.itccoord_mbox_id, mbox->mbox_id);
	}

	for(int i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_delete_mbox != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_delete_mbox(rc, mbox);
			if(rc->flags != ITC_OK)
			{
				free(rc);
				return false;
			}
		}
	}

	mbox->p_rxq_info = NULL;
	strcpy(mbox->name, "");
	mbox->tid = 0;

	rc->flags = ITC_OK;
	q_enqueue(rc, itc_inst.free_mboxes_queue, mbox);
	if(rc->flags != ITC_OK)
	{
		free(rc);
		return false;
	}

	free(rc);
	return true;
}

bool itc_send_zz(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from)
{
	struct itc_message* message;
	struct itc_mailbox* from_mbox;
	struct itc_mailbox* to_mbox;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_nr_mboxes == 0)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return false;
	}

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		if(from == my_threadlocal_mbox[i]->mbox_id)
		{
			from_mbox = my_threadlocal_mbox[i];
			break;
		}
	}

	if(from_mbox == NULL)
	{
		// Not allowed to use mailboxes of other threads to send messages
		free(rc);
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		free(rc);
		return false;
	}

	message = CONVERT_TO_MESSAGE(msg);
	message->sender = from;

	to_mbox = find_mbox(to);
	if(to_mbox != NULL)
	{
		// Local mailbox
		if(to_mbox->mbox_state != MBOX_INUSE)
		{
			// Send a message to a non-active mailbox
			free(rc);
			return false;
		}

		MUTEX_LOCK(rc, &(to_mbox->rxq_info.rxq_mtx));
	}

	int idx = 0;
	for(; idx < ITC_NUM_TRANS; idx++)
	{
		if(trans_mechanisms[idx].itci_trans_send != NULL)
		{
			rc->flags = iTC_OK;
			trans_mechanisms[idx].itci_trans_send(rc, message, to);
			if(rc->flags != ITC_OK)
			{
				if(to_mbox != NULL)
				{
					MUTEX_UNLOCK(rc, &(to_mbox->p_rxq_info->rxq_mtx));
				}
			} else
			{
				break;
			}
		}
	}

	if(idx == ITC_NUM_TRANS)
	{
		if(to_mbox != NULL)
		{
			MUTEX_UNLOCK(rc, &(to_mbox->p_rxq_info->rxq_mtx));
		}
		// ERROR trace is needed here. Failed to send the message on all mechanisms
	}

	uint64_t one = 1;
	/* If this is local mailbox, trigger synchronization by two methods:
	* 1. Write to an FD of receiving mailbox -> trigger epoll/poll/select 
	* 2. Release condition variable of receiving mailbox -> unblock pthread_cond_wait of receiving mailbox on itc_receive() */
	if(to_mbox != NULL && to_mbox->p_rxq_info != NULL)
	{
		int saved_cancel_state;

		/* System call write() below will create a cancellation point that can cause this thread get cancelled unexpectedly */
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &saved_cancel_state);

		if(to_mbox->p_rxq_info->is_fd_created && to_mbox->p_rxq_info->rxq_len == 0)
		{
			if(write(to_mbox->p_rxq_info->rxq_fd, &one, 8) < 0)
			{
				// ERROR trace is needed here
			}
		}

		to_mbox->p_rxq_info->rxq_len++;
		pthread_cond_signal(&(to_mbox->p_rxq_info->rxq_cond));
		MUTEX_UNLOCK(rc, &(to_mbox->p_rxq_info->rxq_mtx));

		pthread_setcancelstate(saved_cancel_state, NULL);
	}

	*msg = NULL;

	free(rc);
	return true;
}

union itc_msg *itc_receive_zz(uint32_t tmo, itc_mbox_id_t from)
{
	struct itc_message* message = NULL;
	struct itc_mailbox* from_mbox;
	struct timespec ts;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return NULL;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_nr_mboxes == 0)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return NULL;
	}

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		if(from == my_threadlocal_mbox[i]->mbox_id)
		{
			from_mbox = my_threadlocal_mbox[i];
			break;
		}
	}

	if(from_mbox == NULL)
	{
		// "from" mailbox is not from this thread
		free(rc);
		return NULL;
	}

	if(tmo != ITC_NO_TMO && tmo > 0)
	{
		calc_time(&ts, tmo);
	}

	do
	{
		MUTEX_LOCK(rc, &(from_mbox->p_rxq_info->rxq_mtx));

		from_mbox->p_rxq_info->is_in_rx = true;
		
		for(int i = 0; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_receive != NULL)
			{
				message = trans_mechanisms[i].itci_trans_receive(rc, from_mbox);
			}
		}

		if(message == NULL)
		{
			if(tmo == 0)
			{
				/* If nothing in rx queue, return immediately */
				from_mbox->p_rxq_info->is_in_rx = false;
				MUTEX_UNLOCK(rc, &(from_mbox->p_rxq_info->rxq_mtx));
				break;
			} else if(tmo == ITC_NO_TMO)
			{
				/* Wait undefinitely until we receive something from rx queue */
				if(pthread_cond_wait(&(from_mbox->p_rxq_info->rxq_cond), &(from_mbox->p_rxq_info->rxq_mtx)) != 0)
				{
					// ERROR trace is needed here
					free(rc);
					return NULL;
				}
			} else
			{
				int ret = pthread_cond_timedwait(&(from_mbox->p_rxq_info->rxq_cond), &(from_mbox->p_rxq_info->rxq_mtx), &ts);
				if(ret == ETIMEDOUT)
				{
					from_mbox->p_rxq_info->is_in_rx = false;
					MUTEX_UNLOCK(rc, &(from_mbox->p_rxq_info->rxq_mtx));
					break;
				} else if(ret != 0)
				{
					// ERROR trace is needed here
					free(rc);
					return NULL;
				}
			}
		} else
		{
			from_mbox->p_rxq_info->rxq_len--;
			if(from_mbox->p_rxq_info->is_fd_created && from_mbox->p_rxq_info->rxq_len == 0)
			{
				char readbuf[8];
				if(read(from_mbox->p_rxq_info->rxq_fd, &readbuf, 8) < 0)
				{
					// ERROR trace is needed here
					free(rc);
					return NULL;
				}
			}
			
			
		}

		from_mbox->p_rxq_info->is_in_rx = false;
		
		MUTEX_UNLOCK(rc, &(from_mbox->p_rxq_info->rxq_mtx));
	} while(message == NULL);

	free(rc);
	return (union itc_msg*)((message == NULL) ? NULL : CONVERT_TO_MSG(message));
}

itc_mbox_id_t itc_sender_zz(union itc_msg *msg)
{
	struct itc_message* message;
	message = CONVERT_TO_MESSAGE(msg);

	return message->sender;
}

itc_mbox_id_t itc_receiver_zz(union itc_msg *msg)
{
	struct itc_message* message;
	message = CONVERT_TO_MESSAGE(msg);

	return message->receiver;
}

size_t itc_size_zz(union itc_msg *msg)
{
	struct itc_message* message;
	message = CONVERT_TO_MESSAGE(msg);

	return message->size;
}

itc_mbox_id_t* itc_current_mboxes_zz()
{
	static __thread itc_mbox_id_t mbox_id_arr[my_threadlocal_nr_mboxes + 1];

	mbox_id_arr[0] = my_threadlocal_nr_mboxes;
	for(int i = 1; i <= my_threadlocal_nr_mboxes; i++)
	{
		mbox_id_arr[i] = my_threadlocal_mbox[i-1]->mbox_id;
	}

	return mbox_id_arr;
}

int itc_get_fd_zz(itc_mbox_id_t mbox_id)
{
	struct itc_mailbox* mbox;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return -1;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_nr_mboxes == 0)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return -1;
	}

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		if(mbox_id == my_threadlocal_mbox[i]->mbox_id)
		{
			mbox = my_threadlocal_mbox[i];
			break;
		}
	}

	if(mbox == NULL)
	{
		// "mbox_id" mailbox is not from this thread
		free(rc);
		return -1;
	}

	MUTEX_LOCK(rc, &(mbox->p_rxq_info->rxq_mtx));

	uint64_t one = 1;
	if(!mbox->p_rxq_info->is_fd_created)
	{
		mbox->p_rxq_info->rxq_fd eventfd(0, 0);
		if(mbox->p_rxq_info->rxq_fd == -1)
		{
			MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
			free(rc);
			return -1;
		}
		mbox->p_rxq_info->is_fd_created = true;
		if(mbox->p_rxq_info->rxq_len != 0)
		{
			if(write(mbox->p_rxq_info->rxq_fd, &one, 8) < 0)
			{
				MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
				free(rc);
				return -1;
			}
		}
	}

	MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
	free(rc);
	return mbox->p_rxq_info->rxq_fd;
}

bool itc_get_name_zz(itc_mbox_id_t mbox_id, char *name)
{
	struct itc_mailbox* mbox;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_nr_mboxes == 0)
	{
		// Not initialized yet
		// ERROR trace is needed
		free(rc);
		return false;
	}

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		if(mbox_id == my_threadlocal_mbox[i]->mbox_id)
		{
			mbox = my_threadlocal_mbox[i];
			break;
		}
	}

	if(mbox == NULL)
	{
		// "mbox_id" mailbox is not from this thread
		free(rc);
		return false;
	}

	strcpy(name, mbox->name);
	free(rc);
	return true;
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_all_itc_resources()
{
	if(pthread_key_delete(itc_inst.destruct_key) != 0)
	{
		perror("release_all_itc_resources - pthread_key_delete");
		return;
	}

	if(itc_inst.name_space)
	{
		free(itc_inst.name_space);
	}

	free(itc_inst.mboxes);
	memset(&itc_inst, 0, sizeof(struct itc_instance));

	for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
	{
		my_threadlocal_mbox[i] = NULL;
	}
}

/* By any reason, a thread in a process is terminated, the thread-specific local data that is associated with a pthread destruct key
* (key is linked to a destructor function) will call its respective destructor function */
/* This function is assigned to "itc_mailbox*" of a thread */
/* Here "void* data" is the thread-local data */
static void mailbox_destructor_at_thread_exit(void* data)
{
	struct itc_mailbox* mbox = (struct itc_mailbox*)data;

	if(itc_inst.mboxes != NULL && mbox->mbox_state == MBOX_INUSE)
	{
		for(int i = 0; i < my_threadlocal_nr_mboxes; i++)
		{
			if(my_threadlocal_mbox[i] == mbox)
			{
				itc_delete_mailbox(mbox->mbox_id);
			}
		}
	}
}

static itc_mailbox* find_mbox(itc_mbox_id_t mbox_id)
{
	/* This mailbox belongs to this process or not */
	if((mbox_id & itc_inst.itccoord_mask) == itc_inst.my_mbox_id_in_itccoord)
	{
		uint32_t mbox_index = (uint32_t)(mbox_id & itc_inst.local_mbox_mask);
		if(mbox_index < (uint32_t)itc_inst.nr_mboxes)
		{
			return itc_inst.mboxes[mbox_index];
		}
	}

	return NULL;
}

static void calc_time(struct timespec* ts, unsigned long tmo)
{
	clock_gettime(CLOCK_MONOTONIC, ts);

	ts->tv_sec	+= tmo / 1000;
	ts->tv_nsec	+= (tmp % 1000) * 1000000;

	if(ts->tv_nsec >= 1000000000)
	{
		ts->tv_sec	+= 1;
		ts->tv_nsec	-= 1000000000;
	} 
}