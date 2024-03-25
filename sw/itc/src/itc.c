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
static __thread struct itc_mailbox*	my_threadlocal_mbox = NULL; // A thread only owns one mailbox

extern struct itci_transport_apis local_trans_apis;
extern struct itci_transport_apis sysvmq_trans_apis;
extern struct itci_transport_apis lsock_trans_apis;

extern struct itci_alloc_apis malloc_apis;

/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_all_itc_resources(void);
static void mailbox_destructor_at_thread_exit(void* data);
static struct itc_mailbox* find_mbox(itc_mbox_id_t mbox_id);
static void calc_abs_time(struct timespec* ts, unsigned long tmo);


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
bool itc_init_zz(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags)
{
	(void)namespace; // Will be implemented later

	int max_msgsize = ITC_MAX_MSGSIZE;
	uint32_t flags = 0;
	int ret = 0;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		perror("\tDEBUG: itc_init_zz - malloc");
                return false;
	}

	/* Re-init */
	if(itc_inst.mboxes != NULL)
	{
		if(getpid() == itc_inst.pid)
		{
			/* Already initialized */
			printf("\tDEBUG: itc_init_zz - Already initialized!\n");
			
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
			printf("\tDEBUG: itc_init_zz - Force re-initializing!\n");
		}
	}

	itc_inst.pid = getpid();

	ret = pthread_mutex_init(&itc_inst.thread_list_mtx, NULL);
	if(ret != 0)
	{
		printf("\tDEBUG: itc_init_zz - pthread_mutex_init error code = %d\n", ret);
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

	for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_maxmsgsize != NULL)
		{
			int msgsize = 0;

			msgsize = trans_mechanisms[i].itci_trans_maxmsgsize(rc);
			if(msgsize > max_msgsize)
			{
				max_msgsize = msgsize;
			}
		}
	}

	if(alloc_mechanisms.itci_alloc_init != NULL)
	{
		alloc_mechanisms.itci_alloc_init(rc, max_msgsize);
	}

	if(init_flags & ITC_FLAGS_I_AM_ITC_COORD)
	{
		itc_inst.itccoord_mask = ITC_COORD_MASK;
		itc_inst.itccoord_mbox_id = 1 << ITC_COORD_SHIFT;
		itc_inst.my_mbox_id_in_itccoord = (1 << ITC_COORD_SHIFT) | 1;
	} else
	{
		uint32_t i = 0;
		for(; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_locate_itccoord != NULL && \
			trans_mechanisms[i].itci_trans_locate_itccoord(rc, &itc_inst.my_mbox_id_in_itccoord, &itc_inst.itccoord_mask, &itc_inst.itccoord_mbox_id))
			{
				break;
			}
		}

#ifdef UNITTEST
		// In unit test, we assume that connecting to itccoord is successful
		itc_inst.itccoord_mask = ITC_COORD_MASK;
		itc_inst.itccoord_mbox_id = 1 << ITC_COORD_SHIFT | 1;
		itc_inst.my_mbox_id_in_itccoord = 5 << ITC_COORD_SHIFT;
		rc->flags = ITC_OK; // Reset the rc->flags because locating itccoord above populated the flags due to failures
#else
		/* Failed to locate itccoord which is not acceptable! */
		if(i == ITC_NUM_TRANS)
		{
			printf("\tDEBUG: itc_init_zz - Failed to locate itccoord!\n");
			free(rc);
			return false;
		}
#endif
	}


	itc_inst.mboxes = (struct itc_mailbox*)malloc(nr_mboxes*sizeof(struct itc_mailbox));
	if(itc_inst.mboxes == NULL)
	{
		perror("\tDEBUG: itc_init_zz - malloc");
		free(rc);
		return false;
	}
	memset(itc_inst.mboxes, 0, nr_mboxes*sizeof(struct itc_mailbox));

	itc_inst.local_mbox_mask = 0xFFFFFFFF >> CLZ(nr_mboxes);
	itc_inst.nr_mboxes = nr_mboxes;

	struct itc_mailbox* mbox_iter;
	pthread_condattr_t condattr;
	for(uint32_t i=0; i < itc_inst.nr_mboxes; i++)
	{
		mbox_iter = &itc_inst.mboxes[i];
		mbox_iter->mbox_id = itc_inst.my_mbox_id_in_itccoord | i;

		ret = pthread_mutexattr_init(&(mbox_iter->rxq_info.rxq_attr));
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_mutexattr_init error code = %d\n", ret);
			free(rc);
			return false;
		}

		/* Use this type of mutex is safetest, check man7 page for details */
		ret = pthread_mutexattr_settype(&(mbox_iter->rxq_info.rxq_attr), PTHREAD_MUTEX_ERRORCHECK);
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_mutexattr_settype error code = %d\n", ret);
			free(rc);
			return false;
		}
		
		ret = pthread_mutex_init(&(mbox_iter->rxq_info.rxq_mtx), &(mbox_iter->rxq_info.rxq_attr));
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_mutex_init error code = %d\n", ret);
			free(rc);
			return false;
		}

		ret = pthread_condattr_init(&condattr);
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_condattr_init error code = %d\n", ret);
			free(rc);
			return false;
		}

		/* Use clock that indicates the period of time in second from when the system is booted to measure time serving for pthread_cond_timedwait */
		ret = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_condattr_setclock error code = %d\n", ret);
			free(rc);
			return false;
		}

		ret = pthread_cond_init(&(mbox_iter->rxq_info.rxq_cond), &condattr);
		if(ret != 0)
		{
			printf("\tDEBUG: itc_init_zz - pthread_cond_init error code = %d\n", ret);
			free(rc);
			return false;
		}
		
		if(i == 0)
		{
			printf("\tDEBUG: itc_init_zz - q_init the first mailbox to itc_inst.free_mbox_queue!\n");
			itc_inst.free_mboxes_queue = q_init(rc);
		}

		printf("\tDEBUG: itc_init_zz - q_enqueue mailbox %u to itc_inst.free_mbox_queue!\n", i);
		q_enqueue(rc, itc_inst.free_mboxes_queue, mbox_iter);
	}

	ret = pthread_key_create(&itc_inst.destruct_key, mailbox_destructor_at_thread_exit);
	if(ret != 0)
	{
		printf("\tDEBUG: itc_init_zz - pthread_key_create error code = %d\n", ret);
		free(rc);
		return false;
	}

	for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_init != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_init(rc, itc_inst.my_mbox_id_in_itccoord, \
						itc_inst.itccoord_mask, itc_inst.nr_mboxes, flags);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				printf("\tDEBUG: itc_free_zz - Failed to init trans_mechanism[%u]!\n", i);
				free(rc);
				return false;
			}
		}
	}

	start_itcthreads(rc); // Start sysvmq_rx_thread
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: itc_free_zz - Failed to start_itcthreads!\n");
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
		perror("\tDEBUG: itc_exit_zz - malloc");
                return false;
	}

	/* If itc_inst.mboxes == NULL, only two possible cases. One is mutex_init failed, or failed to locate itccoord */
	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_exit_zz - itc_inst.mboxes == NULL!\n");
		free(rc);
		return false;
	}

	for(uint32_t i = 0; i < itc_inst.nr_mboxes; i++)
	{
		mbox = &itc_inst.mboxes[i];
		
		MUTEX_LOCK(rc, &mbox->rxq_info.rxq_mtx);

		if(mbox->mbox_state == MBOX_INUSE)
		{
			running_mboxes++;
		}

		MUTEX_UNLOCK(rc, &mbox->rxq_info.rxq_mtx);

		if(running_mboxes > ITC_NR_INTERNAL_USED_MBOXES)
		{
			// ERROR trace is needed here
			printf("\tDEBUG: itc_exit_zz - Still had %u remaining open mailboxes!\n", running_mboxes);
			free(rc);
			return false;
		}
	}

	rc->flags = ITC_OK;
	terminate_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: itc_exit_zz - Failed to terminate_itcthreads!\n");
		free(rc);
		return false;
	}

	int ret = 0;
	for(uint32_t i = 0; i < itc_inst.nr_mboxes; i++)
	{
		mbox = &itc_inst.mboxes[i];

		ret = pthread_cond_destroy(&(mbox->rxq_info.rxq_cond));
		if(ret != 0)
		{
			// ERROR trace is needed here
			printf("\tDEBUG: itc_exit_zz - pthread_cond_destroy error code = %d\n", ret);
			free(rc);
			return false;
		}

		ret = pthread_mutex_destroy(&(mbox->rxq_info.rxq_mtx));
		if(ret != 0)
		{
			// ERROR trace is needed here
			printf("\tDEBUG: itc_exit_zz - pthread_mutex_destroy error code = %d\n", ret);
			free(rc);
			return false;
		}

		ret = pthread_mutexattr_destroy(&(mbox->rxq_info.rxq_attr));
		if(ret != 0)
		{
			// ERROR trace is needed here
			printf("\tDEBUG: itc_exit_zz - pthread_mutexattr_destroy error code = %d\n", ret);
			free(rc);
			return false;
		}
	}

	for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_exit != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_exit(rc);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				printf("\tDEBUG: itc_exit_zz - Failed to exit on trans_mechanism[%d]!\n", i);
				free(rc);
				return false;
			}
		}
	}

	ret = pthread_key_delete(itc_inst.destruct_key);
	if(ret != 0)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: itc_exit_zz - pthread_key_delete error code = %d\n", ret);
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
		printf("\tDEBUG: itc_exit_zz - q_exit!\n");
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
		perror("\tDEBUG: itc_alloc_zz - malloc");
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
		printf("\tDEBUG: itc_alloc_zz - Not initialized yet!\n");
		free(rc);
		return NULL;
	}

	message = alloc_mechanisms.itci_alloc_alloc(rc, size + ITC_HEADER_SIZE + 1);
	if(message == NULL)
	{
		printf("\tDEBUG: itc_alloc_zz - Failed to allocate message!\n");
		free(rc);
		return NULL;
	}

	message->msgno = msgno;
	message->sender = 0;
	message->receiver = 0;
	message->size = size;
	message->flags = 0;
	endpoint = (char*)((unsigned long)(&message->msgno) + size);
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
		perror("\tDEBUG: itc_free_zz - malloc");
                return false;
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_free_zz - Not initialized yet!\n");
		free(rc);
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		printf("\tDEBUG: itc_free_zz - Double free!\n");
		free(rc);
		return false;
	}

	message = CONVERT_TO_MESSAGE(*msg);
	endpoint = (char*)((unsigned long)(&message->msgno) + message->size);

	if(message->flags & ITC_FLAGS_MSG_INRXQUEUE)
	{
		printf("\tDEBUG: itc_free_zz - Message still in rx queue!\n");
		free(rc);
		return false;
	} else if(*endpoint != ENDPOINT)
	{
		printf("\tDEBUG: itc_free_zz - Invalid *endpoint = 0x%1x!\n", *endpoint & 0xFF);
		free(rc);
		return false;
	}

	rc->flags = ITC_OK;
	alloc_mechanisms.itci_alloc_free(rc, &message);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: itc_free_zz - Failed to free message!\n");
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
		perror("\tDEBUG: itc_create_mailbox_zz - malloc");
                return ITC_NO_MBOX_ID;
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_create_mailbox_zz - Not initialized yet!\n");
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	if(my_threadlocal_mbox != NULL)
	{
		printf("\tDEBUG: itc_create_mailbox_zz - This thread already had a mailbox!\n");
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	new_mbox = q_dequeue(rc, itc_inst.free_mboxes_queue);
	if(new_mbox == NULL)
	{
		printf("\tDEBUG: itc_create_mailbox_zz - Not enough available mailbox to create!\n");
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	if(strlen(name) > (ITC_MAX_MBOX_NAME_LENGTH))
	{
		printf("\tDEBUG: itc_create_mailbox_zz - Requested mailbox name too long!\n");
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

	for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_create_mbox != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_create_mbox(rc, new_mbox, flags);
			if(rc->flags != ITC_OK)
			{
				// ERROR trace is needed here
				printf("\tDEBUG: itc_create_mailbox_zz - Failed to create mailbox on trans_mechanism[%d]!\n", i);
				MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));
				free(rc);
				return ITC_NO_MBOX_ID;
			}
		}
	}

	int ret = pthread_setspecific(itc_inst.destruct_key, new_mbox);
	if(ret != 0)
	{
		// ERROR trace is needed here
		printf("\tDEBUG: itc_create_mailbox_zz - pthread_setspecific error code = %d\n", ret);
		MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));
		free(rc);
		return ITC_NO_MBOX_ID;
	}

	my_threadlocal_mbox = new_mbox;

	MUTEX_UNLOCK(rc, &(new_mbox->p_rxq_info->rxq_mtx));

	/* In case this process is not the itccoord process, send notification to itccoord */
	if(itc_inst.my_mbox_id_in_itccoord != (itc_inst.itccoord_mbox_id & itc_inst.itccoord_mask))
	{
		msg = itc_alloc(offsetof(struct itc_notify_coord_add_rmv_mbox, mbox_name) + strlen(name) + 1, ITC_NOTIFY_COORD_ADD_MBOX);
		msg->itc_notify_coord_add_rmv_mbox.mbox_id = new_mbox->mbox_id;
		strcpy(msg->itc_notify_coord_add_rmv_mbox.mbox_name, name);
		bool res = itc_send(&msg, itc_inst.itccoord_mbox_id, new_mbox->mbox_id);
		if(!res)
		{
			printf("\tDEBUG: itc_create_mailbox_zz - Failed to send notification to itccoord regarding ADD mailbox id = %u!\n", new_mbox->mbox_id);
			itc_free(&msg);
		} else
		{
			printf("\tDEBUG: itc_create_mailbox_zz - Sent notification to itccoord regarding ADD mailbox id = %u!\n", new_mbox->mbox_id);
		}
		
	}

	free(rc);
	return new_mbox->mbox_id;
}

bool itc_delete_mailbox_zz(itc_mbox_id_t mbox_id)
{
	struct itc_mailbox* mbox;
	union itc_msg* msg;
	pthread_mutex_t* rxq_mtx;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		perror("\tDEBUG: itc_delete_mailbox_zz - malloc");
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_delete_mailbox_zz - Not initialized yet!\n");
		free(rc);
		return false;
	}

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// Not allowed to delete a mailbox of other threads
		printf("\tDEBUG: itc_delete_mailbox_zz - Not allowed to delete other thread's mailbox, mbox_id = %u!\n", mbox_id);
		free(rc);
		return false;
	}

	mbox = my_threadlocal_mbox;

	rxq_mtx = &(mbox->p_rxq_info->rxq_mtx);
#ifdef MUTEX_TRACE_UNITTEST
	printf("\tDEBUG: MUTEX_LOCK\t0x%08lx!\n", (unsigned long)rxq_mtx);
#endif
	int res = pthread_mutex_lock(rxq_mtx);
	if(res != 0 && res != EDEADLK)
	{
		printf("\tDEBUG: itc_delete_mailbox_zz - pthread_mutex_lock error code = %d\n", res);
		free(rc);
		return false;
	}

	mbox->mbox_state = MBOX_UNUSED;

	MUTEX_UNLOCK(rc, rxq_mtx);

	if(mbox->p_rxq_info->is_fd_created)
	{
		if(close(mbox->p_rxq_info->rxq_fd) == -1)
		{
			// ERROR trace is needed here
			perror("\tDEBUG: itc_delete_mailbox_zz - close");
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
		bool res = itc_send(&msg, itc_inst.itccoord_mbox_id, mbox->mbox_id);
		if(!res)
		{
			printf("\tDEBUG: itc_delete_mailbox_zz - Failed to send notification to itccoord regarding RMV mailbox id = %u!\n", mbox->mbox_id);
			itc_free(&msg);
		} else
		{
			printf("\tDEBUG: itc_delete_mailbox_zz - Sent notification to itccoord about my demise mbox_id = %u!\n", mbox->mbox_id);
		}
	}

	for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
	{
		if(trans_mechanisms[i].itci_trans_delete_mbox != NULL)
		{
			rc->flags = ITC_OK;
			trans_mechanisms[i].itci_trans_delete_mbox(rc, mbox);
			if(rc->flags != ITC_OK)
			{
				printf("\tDEBUG: itc_create_mailbox_zz - Failed to delete mailbox on trans_mechanism[%d]!\n", i);
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
		printf("\tDEBUG: itc_delete_mailbox_zz - Failed to q_enqueue!\n");
		free(rc);
		return false;
	}

	MUTEX_UNLOCK(rc, rxq_mtx);

	my_threadlocal_mbox = NULL;

	printf("\tDEBUG: itc_delete_mailbox_zz - Delete thread-local mailbox %u, threadlocal_mbox list updated!\n", mbox_id);
	free(rc);
	return true;
}

bool itc_send_zz(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from)
{
	struct itc_message* message;
	struct itc_mailbox* to_mbox;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		perror("\tDEBUG: itc_send_zz - malloc");
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_send_zz - Not initialized yet!\n");
		free(rc);
		return false;
	}

	if(to == my_threadlocal_mbox->mbox_id)
	{
		printf("\tDEBUG: itc_send_zz - Not allowed to send messages to myself, which causes deadlock, from = %u, to = %u!\n", from, to);
		free(rc);
		return false;
	}

	if(from != my_threadlocal_mbox->mbox_id)
	{
		// Not allowed to use mailboxes of other threads to send messages
		printf("\tDEBUG: itc_send_zz - Not allowed to use other thread's mailbox to send messages, mbox_id = %u!\n", from);
		free(rc);
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		printf("\tDEBUG: itc_send_zz - The sending message is NULL!\n");
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
			printf("\tDEBUG: itc_send_zz - Sending message to a non-active mailbox!\n");
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
			rc->flags = ITC_OK;
			trans_mechanisms[idx].itci_trans_send(rc, message, to);
			if(rc->flags != ITC_OK)
			{
				if(to_mbox != NULL)
				{
					MUTEX_UNLOCK(rc, &(to_mbox->p_rxq_info->rxq_mtx));
				}
			} else
			{
				printf("\tDEBUG: itc_send_zz - Sent successfully on trans_mechanism[%u]!\n", idx);
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
		printf("\tDEBUG: itc_send_zz - Failed to send message by all transport mechanisms!\n");
		free(rc);
		return false;
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
				perror("\tDEBUG: itc_send_zz - write");
			}
		}

		to_mbox->p_rxq_info->rxq_len++;
		pthread_cond_signal(&(to_mbox->p_rxq_info->rxq_cond));
		MUTEX_UNLOCK(rc, &(to_mbox->p_rxq_info->rxq_mtx));

		pthread_setcancelstate(saved_cancel_state, NULL);
		printf("\tDEBUG: itc_send_zz - Notify receiver about sent messages!\n");
	}

	*msg = NULL;

	free(rc);
	return true;
}

union itc_msg *itc_receive_zz(int32_t tmo, itc_mbox_id_t from)
{
	struct itc_message* message = NULL;
	struct itc_mailbox* mbox;
	struct timespec ts;

	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		perror("\tDEBUG: itc_receive_zz - malloc");
                return NULL;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_receive_zz - Not initialized yet!\n");
		free(rc);
		return NULL;
	}

	if(from == my_threadlocal_mbox->mbox_id)
	{
		printf("\tDEBUG: itc_receive_zz - Not allowed to receive messages from myself, from_mbox_id %u !\n", from);
		free(rc);
		return false;
	}

	mbox = my_threadlocal_mbox;

	if(tmo != ITC_NO_TMO && tmo > 0)
	{
		calc_abs_time(&ts, tmo);
	}

	do
	{
		MUTEX_LOCK(rc, &(mbox->p_rxq_info->rxq_mtx));

		mbox->p_rxq_info->is_in_rx = true;
		
		for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_receive != NULL)
			{
				message = trans_mechanisms[i].itci_trans_receive(rc, mbox);
				if(message != NULL)
				{
					printf("\tDEBUG: itc_receive_zz - Received a message on trans_mechanisms[%u]!\n", i);
					break;
				}
			}
		}

		if(message == NULL)
		{
			if(tmo == 0)
			{
				/* If nothing in rx queue, return immediately */
				printf("\tDEBUG: itc_receive_zz - No message in rx queue, return!\n");
				mbox->p_rxq_info->is_in_rx = false;
				MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
				break;
			} else if(tmo == ITC_NO_TMO)
			{
				/* Wait undefinitely until we receive something from rx queue */
				int ret = pthread_cond_wait(&(mbox->p_rxq_info->rxq_cond), &(mbox->p_rxq_info->rxq_mtx));
				if(ret != 0)
				{
					// ERROR trace is needed here
					printf("\tDEBUG: itc_receive_zz - pthread_cond_wait error code = %d\n", ret);
					free(rc);
					return NULL;
				}
			} else
			{
				int ret = pthread_cond_timedwait(&(mbox->p_rxq_info->rxq_cond), &(mbox->p_rxq_info->rxq_mtx), &ts);
				if(ret == ETIMEDOUT)
				{
					printf("\tDEBUG: itc_receive_zz - Timeout when expecting message, timeout = %u ms!\n", tmo);
					mbox->p_rxq_info->is_in_rx = false;
					MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
					break;
				} else if(ret != 0)
				{
					// ERROR trace is needed here
					printf("\tDEBUG: itc_receive_zz - pthread_cond_timedwait error code = %d\n", ret);
					free(rc);
					return NULL;
				}
			}
		} else
		{
			mbox->p_rxq_info->rxq_len--;
			if(mbox->p_rxq_info->is_fd_created && mbox->p_rxq_info->rxq_len == 0)
			{
				char readbuf[8];
				if(read(mbox->p_rxq_info->rxq_fd, &readbuf, 8) < 0)
				{
					// ERROR trace is needed here
					perror("\tDEBUG: itc_receive_zz - read");
					free(rc);
					return NULL;
				}
			}
			
			
		}

		mbox->p_rxq_info->is_in_rx = false;
		
		MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
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

itc_mbox_id_t itc_current_mbox_zz()
{
	if(my_threadlocal_mbox != NULL)
	{
		printf("\tDEBUG: itc_current_mboxes_zz - This thread has mailbox with id = %u!\n", my_threadlocal_mbox->mbox_id);
		return my_threadlocal_mbox->mbox_id;
	}

	printf("\tDEBUG: itc_current_mboxes_zz - This thread has no mailbox!\n");
	return ITC_NO_MBOX_ID;
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
		perror("\tDEBUG: itc_get_fd_zz - malloc");
                return -1;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_get_fd_zz - Not initialized yet!\n");
		free(rc);
		return -1;
	}

	mbox = my_threadlocal_mbox;

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// "mbox_id" mailbox is not from this thread
		printf("\tDEBUG: itc_get_fd_zz - Mailbox not owned by this thread, mbox_id = %u, this thread's mbox_id = %u!\n", mbox_id, mbox->mbox_id);
		free(rc);
		return false;
	}

	MUTEX_LOCK(rc, &(mbox->p_rxq_info->rxq_mtx));

	uint64_t one = 1;
	if(!mbox->p_rxq_info->is_fd_created)
	{
		mbox->p_rxq_info->rxq_fd = eventfd(0, 0);
		if(mbox->p_rxq_info->rxq_fd == -1)
		{
			perror("\tDEBUG: itc_get_fd_zz - eventfd");
			MUTEX_UNLOCK(rc, &(mbox->p_rxq_info->rxq_mtx));
			free(rc);
			return -1;
		}
		mbox->p_rxq_info->is_fd_created = true;
		if(mbox->p_rxq_info->rxq_len != 0)
		{
			if(write(mbox->p_rxq_info->rxq_fd, &one, 8) < 0)
			{
				perror("\tDEBUG: itc_get_fd_zz - write");
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
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		perror("\tDEBUG: itc_get_name_zz - malloc");
                return false;
	}

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		printf("\tDEBUG: itc_get_name_zz - Not initialized yet!\n");
		free(rc);
		return false;
	}

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// "mbox_id" mailbox is not from this thread
		printf("\tDEBUG: itc_get_name_zz - Mailbox not owned by this thread, mbox_id = %u, this thread's mbox_id = %u!\n", mbox_id, my_threadlocal_mbox->mbox_id);
		free(rc);
		return false;
	}

	strcpy(name, my_threadlocal_mbox->name);
	free(rc);
	return true;
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_all_itc_resources()
{
	int ret = pthread_key_delete(itc_inst.destruct_key);
	if(ret != 0)
	{
		printf("\tDEBUG: release_all_itc_resources - pthread_key_delete error code = %d\n", ret);
		return;
	}

	// if(itc_inst.name_space)
	// {
	// 	free(itc_inst.name_space);
	// }

	free(itc_inst.mboxes);
	memset(&itc_inst, 0, sizeof(struct itc_instance));

	my_threadlocal_mbox = NULL;
}

/* By any reason, a thread in a process is terminated, the thread-specific local data that is associated with a pthread destruct key
* (key is linked to a destructor function) will call its respective destructor function */
/* This function is assigned to "itc_mailbox*" of a thread */
/* Here "void* data" is the thread-local data */
static void mailbox_destructor_at_thread_exit(void* data)
{
	struct itc_mailbox* mbox = (struct itc_mailbox*)data;

	printf("\tDEBUG: mailbox_destructor_at_thread_exit - Thread-local mailbox destructor called by tid = %u, mbox_id = %u!\n", mbox->tid, mbox->mbox_id);
	if(itc_inst.mboxes != NULL && mbox->mbox_state == MBOX_INUSE)
	{
		if(my_threadlocal_mbox == mbox)
		{
			printf("\tDEBUG: mailbox_destructor_at_thread_exit - Deleting mailbox with mbox_id = %u!\n", mbox->mbox_id);
			itc_delete_mailbox(mbox->mbox_id);
		}
	}
}

static struct itc_mailbox* find_mbox(itc_mbox_id_t mbox_id)
{
	/* This mailbox belongs to this process or not */
	if((mbox_id & itc_inst.itccoord_mask) == itc_inst.my_mbox_id_in_itccoord)
	{
		uint32_t mbox_index = (uint32_t)(mbox_id & itc_inst.local_mbox_mask);
		if(mbox_index < (uint32_t)itc_inst.nr_mboxes)
		{
			return &itc_inst.mboxes[mbox_index];
		}
	}

	printf("\tDEBUG: find_mbox - Mailbox not belong to this process, mbox_id = %u!\n", mbox_id);
	return NULL;
}

static void calc_abs_time(struct timespec* ts, unsigned long tmo)
{
	clock_gettime(CLOCK_MONOTONIC, ts);

	ts->tv_sec	+= tmo / 1000;
	ts->tv_nsec	+= (tmo % 1000) * 1000000;

	if(ts->tv_nsec >= 1000000000)
	{
		ts->tv_sec	+= 1;
		ts->tv_nsec	-= 1000000000;
	} 
}