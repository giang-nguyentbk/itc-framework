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
#define ITC_GATEWAY_MBOX_UDP_NAME2	"itc_gw_udp_mailbox2" // TEST ONLY
#define ITC_GATEWAY_MBOX_TCP_CLI_NAME2	"itc_gw_tcp_client_mailbox2" // TEST ONLY

union itc_msg {
	uint32_t				msgno;

	struct itc_notify_coord_add_rmv_mbox	itc_notify_coord_add_rmv_mbox;
	struct itc_locate_mbox_sync_request	itc_locate_mbox_sync_request;
	struct itc_locate_mbox_sync_reply	itc_locate_mbox_sync_reply;
	struct itc_fwd_data_to_itcgws		itc_fwd_data_to_itcgws;
	struct itc_get_namespace_request	itc_get_namespace_request;
	struct itc_get_namespace_reply		itc_get_namespace_reply;
};

struct itc_instance {
	struct itc_queue*		free_mboxes_queue; // a queue of free/has-been-deleted mailboxes that can be re-used later

	itc_mbox_id_t			my_mbox_id_in_itccoord; // mailbox id of this process
	itc_mbox_id_t			itccoord_mask; // mask for itccoord process id, should be 0xFFF0 0000
	itc_mbox_id_t			itccoord_mbox_id;

	struct itc_threads*		thread_list;	// manage a list of threads that is started by itc.c via itc_init() such as sysvmq_rx_thread,...
	pthread_mutex_t			thread_list_mtx;

	pthread_mutex_t			local_locating_mbox_mtx;
	void				*local_locating_mbox_tree;

	pthread_key_t			destruct_key;

	pid_t				pid;

	uint32_t			nr_mboxes;
	uint32_t			local_mbox_mask; // mask for local mailbox id
	struct itc_mailbox*		mboxes; // List of mailboxes allocated by malloc

	char				namespace[ITC_MAX_NAME_LENGTH];	
};

/*****************************************************************************\/
*****                     INTERNAL VARIABLES IN ITC.C                      *****
*******************************************************************************/
struct itc_instance	itc_inst;
static struct itci_transport_apis trans_mechanisms[ITC_NUM_TRANS];
static struct itci_alloc_apis	alloc_mechanisms;

/* When a thread requests for creating a mailbox, there is a itc_mailbox pointer to their mailbox and only it owns its pointer */
static __thread struct itc_mailbox*	my_threadlocal_mbox = NULL; // A thread only owns one mailbox
static __thread struct result_code* rc = NULL; // A thread only owns one return code

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
static struct itc_mailbox *locate_local_mbox(const char *name);
static int mbox_name_cmpfunc(const void *pa, const void *pb); // char *name vs struct itc_mailbox *mbox
static void do_nothing(void *a);
static bool remove_mbox_from_tree(void **tree, pthread_mutex_t *tree_mtx, struct itc_mailbox *mbox);
static int mbox_name_cmpfunc2(const void *pa, const void *pb); // struct itc_mailbox *mbox1 vs struct itc_mailbox *mbox2
static bool insert_mbox_to_tree(void **tree, pthread_mutex_t *tree_mtx, struct itc_mailbox *mbox);
static bool handle_forward_itc_msg_to_itcgw(union itc_msg **msg, itc_mbox_id_t to, char *namespace);


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
bool itc_init_zz(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, uint32_t init_flags)
{
	int max_msgsize = ITC_MAX_MSGSIZE;
	uint32_t flags = 0;
	int ret = 0;

	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
			ITC_DEBUG("Failed to malloc rc for itc_init()!");
                	return false;
		}	
	}

	/* Re-init */
	if(itc_inst.mboxes != NULL)
	{
		if(getpid() == itc_inst.pid)
		{
			/* Already initialized */
			ITC_DEBUG("Already initialized!");
			free(rc);
			return false;
		} else
		{
			if(alloc_mechanisms.itci_alloc_exit != NULL)
			{
				alloc_mechanisms.itci_alloc_exit(rc);
				rc->flags = ITC_OK;
			}
			
			release_all_itc_resources();
			flags = ITC_FLAGS_FORCE_REINIT;
			ITC_DEBUG("Force re-initializing!");
		}
	}

	itc_inst.pid = getpid();

	ret = pthread_mutex_init(&itc_inst.thread_list_mtx, NULL);
	if(ret != 0)
	{
		ITC_DEBUG("pthread_mutex_init error code = %d", ret);
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
			rc->flags = ITC_OK;
			if(msgsize > max_msgsize)
			{
				max_msgsize = msgsize;
			}
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
		itc_inst.itccoord_mbox_id = (1 << ITC_COORD_SHIFT) | 1;
		itc_inst.my_mbox_id_in_itccoord = 1 << ITC_COORD_SHIFT;
	} else
	{
		ITC_DEBUG("Start locating itccoord!");
		uint32_t i = 0;
		for(; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_locate_itccoord != NULL && \
			trans_mechanisms[i].itci_trans_locate_itccoord(rc, &itc_inst.my_mbox_id_in_itccoord, &itc_inst.itccoord_mask, &itc_inst.itccoord_mbox_id))
			{
				break;
			}
		}
		

#if defined MOCK_SENDER_UNITTEST
		// In unit test, we assume that connecting to itccoord is successful
		ITC_DEBUG("Using MOCK_SENDER_UNITTEST!");
		itc_inst.itccoord_mask = ITC_COORD_MASK;
		itc_inst.itccoord_mbox_id = 1 << ITC_COORD_SHIFT | 1;
		itc_inst.my_mbox_id_in_itccoord = 5 << ITC_COORD_SHIFT;
#elif defined MOCK_RECEIVER_UNITTEST
		// In unit test, we assume that connecting to itccoord is successful
		ITC_DEBUG("Using MOCK_RECEIVER_UNITTEST!");
		itc_inst.itccoord_mask = ITC_COORD_MASK;
		itc_inst.itccoord_mbox_id = 1 << ITC_COORD_SHIFT | 1;
		itc_inst.my_mbox_id_in_itccoord = 9 << ITC_COORD_SHIFT;
#else
		/* Failed to locate itccoord which is not acceptable! */
		if(i == ITC_NUM_TRANS)
		{
			ITC_DEBUG("Failed to locate itccoord, rc = %u!", rc->flags);
			free(rc);
			return false;
		}
#endif
		rc->flags = ITC_OK;
	}

	itc_inst.mboxes = (struct itc_mailbox*)malloc(nr_mboxes*sizeof(struct itc_mailbox));
	if(itc_inst.mboxes == NULL)
	{
		ITC_DEBUG("Failed to malloc mailboxes for ict_init()!");
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
			ITC_DEBUG("Failed to pthread_mutexattr_init, error code = %d", ret);
			free(rc);
			return false;
		}

		/* Use this type of mutex is safetest, check man7 page for details */
		ret = pthread_mutexattr_settype(&(mbox_iter->rxq_info.rxq_attr), PTHREAD_MUTEX_ERRORCHECK);
		if(ret != 0)
		{
			ITC_DEBUG("Failed to pthread_mutexattr_settype, error code = %d", ret);
			free(rc);
			return false;
		}
		
		ret = pthread_mutex_init(&(mbox_iter->rxq_info.rxq_mtx), &(mbox_iter->rxq_info.rxq_attr));
		if(ret != 0)
		{
			ITC_DEBUG("Failed to pthread_mutex_init, error code = %d", ret);
			free(rc);
			return false;
		}

		ret = pthread_condattr_init(&condattr);
		if(ret != 0)
		{
			ITC_DEBUG("Failed to pthread_condattr_init, error code = %d", ret);
			free(rc);
			return false;
		}

		/* Use clock that indicates the period of time in second from when the system is booted to measure time serving for pthread_cond_timedwait */
		ret = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
		if(ret != 0)
		{
			ITC_DEBUG("Failed to pthread_condattr_setclock, error code = %d", ret);
			free(rc);
			return false;
		}

		ret = pthread_cond_init(&(mbox_iter->rxq_info.rxq_cond), &condattr);
		if(ret != 0)
		{
			ITC_DEBUG("Failed to pthread_cond_init, error code = %d", ret);
			free(rc);
			return false;
		}
		
		if(i == 0)
		{
			ITC_DEBUG("Running q_init for the first mailbox to itc_inst.free_mbox_queue!");
			itc_inst.free_mboxes_queue = q_init(rc);
			rc->flags = ITC_OK;
		}

		ITC_DEBUG("Running q_enqueue mailbox %u to itc_inst.free_mbox_queue!", i);
		q_enqueue(rc, itc_inst.free_mboxes_queue, mbox_iter);
		rc->flags = ITC_OK;
	}

	ret = pthread_key_create(&itc_inst.destruct_key, mailbox_destructor_at_thread_exit);
	if(ret != 0)
	{
		ITC_DEBUG("Failed to create destruct_key, error code = %d", ret);
		free(rc);
		return false;
	}

	ret = pthread_mutex_init(&itc_inst.local_locating_mbox_mtx, NULL);
	if(ret != 0)
	{
		ITC_DEBUG("Failed to init local_locating_tree mutex, error code = %d", ret);
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
				ITC_DEBUG("Failed to init trans_mechanism[%u]!", i);
				free(rc);
				return false;
			}
		}
	}

	start_itcthreads(rc); // Start sysvmq_rx_thread
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		ITC_DEBUG("Failed to start_itcthreads!");
		free(rc);
		return false;
	}

	return true;
}

bool itc_exit_zz()
{
	struct itc_mailbox* mbox;
	int running_mboxes = 0;

	/* If itc_inst.mboxes == NULL, only two possible cases. One is mutex_init failed, or failed to locate itccoord */
	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("itc_inst.mboxes == NULL!");
		return false;
	}

	rc->flags = ITC_OK;
	for(uint32_t i = 0; i < itc_inst.nr_mboxes; i++)
	{
		mbox = &itc_inst.mboxes[i];
		
		MUTEX_LOCK(&mbox->rxq_info.rxq_mtx);


		if(mbox->mbox_state == MBOX_INUSE)
		{
			running_mboxes++;
		}

		MUTEX_UNLOCK(&mbox->rxq_info.rxq_mtx);

		if(running_mboxes > ITC_NR_INTERNAL_USED_MBOXES)
		{
			// ERROR trace is needed here
			ITC_DEBUG("Still had %u remaining open mailboxes!", running_mboxes);
			return false;
		}
	}

	rc->flags = ITC_OK;
	terminate_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		ITC_DEBUG("Failed to terminate_itcthreads!");
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
			ITC_DEBUG("Failed to pthread_cond_destroy, error code = %d", ret);
			return false;
		}

		ret = pthread_mutex_destroy(&(mbox->rxq_info.rxq_mtx));
		if(ret != 0)
		{
			// ERROR trace is needed here
			ITC_DEBUG("Failed to pthread_mutex_destroy, error code = %d", ret);
			return false;
		}

		ret = pthread_mutexattr_destroy(&(mbox->rxq_info.rxq_attr));
		if(ret != 0)
		{
			// ERROR trace is needed here
			ITC_DEBUG("Failed to pthread_mutexattr_destroy, error code = %d", ret);
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
				ITC_DEBUG("Failed to exit on trans_mechanism[%d]!", i);
				return false;
			}
		}
	}

	ret = pthread_key_delete(itc_inst.destruct_key);
	if(ret != 0)
	{
		// ERROR trace is needed here
		ITC_DEBUG("pthread_key_delete error code = %d", ret);
		return false;
	}

	/* Destroy local_locating_tree */
	MUTEX_LOCK(&itc_inst.local_locating_mbox_mtx);
	tdestroy(itc_inst.local_locating_mbox_tree, do_nothing);
	MUTEX_UNLOCK(&itc_inst.local_locating_mbox_mtx);

	ret = pthread_mutex_destroy(&itc_inst.local_locating_mbox_mtx);
	if(ret != 0)
	{
		ITC_DEBUG("pthread_mutex_destroy error code = %d", ret);
		return false;
	}

	if(alloc_mechanisms.itci_alloc_exit != NULL)
	{
		alloc_mechanisms.itci_alloc_exit(rc);
	}

	free(itc_inst.mboxes);

	rc->flags = ITC_OK;
	ITC_DEBUG("Removing mailboxes from free_mboxes_queue, count = %u!", itc_inst.free_mboxes_queue->size);
	q_exit(rc, itc_inst.free_mboxes_queue);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		ITC_DEBUG("q_exit!");
		return false;
	}

	free(rc);
	rc = NULL;

	return true;
}

union itc_msg *itc_alloc_zz(size_t size, uint32_t msgno)
{
	struct itc_message* message;
	char* endpoint;

	ITC_DEBUG("Allocating itc msg msgno 0x%08x, size = %lu", msgno, size);

	if(size < sizeof(msgno))
	{
		size = sizeof(msgno);
	}

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return NULL;
	}

	rc->flags = ITC_OK;
	message = alloc_mechanisms.itci_alloc_alloc(rc, size + ITC_HEADER_SIZE + 1);
	rc->flags = ITC_OK;
	if(message == NULL)
	{
		ITC_DEBUG("Failed to allocate message!");
		return NULL;
	}

	message->msgno = msgno;
	message->sender = ITC_NO_MBOX_ID;
	message->receiver = ITC_NO_MBOX_ID;
	message->size = size;
	message->flags = 0;
	endpoint = (char*)((unsigned long)(&message->msgno) + size);
	*endpoint = ENDPOINT;

	return (union itc_msg*)&(message->msgno);
}

bool itc_free_zz(union itc_msg **msg)
{
	struct itc_message* message;
	char* endpoint;

	ITC_DEBUG("Freeing itc msg msgno 0x%08x", (*msg)->msgno);

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		ITC_DEBUG("Double free!");
		return false;
	}

	message = CONVERT_TO_MESSAGE(*msg);
	endpoint = (char*)((unsigned long)(&message->msgno) + message->size);

	if(message->flags & ITC_FLAGS_MSG_INRXQUEUE)
	{
		ITC_DEBUG("Message still in rx queue!");
		return false;
	} else if(*endpoint != ENDPOINT)
	{
		ITC_DEBUG("Invalid *endpoint = 0x%1x!", *endpoint & 0xFF);
		return false;
	}

	rc->flags = ITC_OK;
	alloc_mechanisms.itci_alloc_free(rc, &message);
	if(rc->flags != ITC_OK)
	{
		// ERROR trace is needed here
		ITC_DEBUG("Failed to free message!");
		return false;
	}

	*msg = NULL;
	return true;
}

itc_mbox_id_t itc_create_mailbox_zz(const char *name, uint32_t flags)
{
	struct itc_mailbox* new_mbox;
	union itc_msg* msg;

	ITC_DEBUG("Creating mailbox name %s", name);

	if(itc_inst.mboxes == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return ITC_NO_MBOX_ID;
	}

	if(my_threadlocal_mbox != NULL)
	{
		ITC_DEBUG("This thread already had a mailbox!");
		return ITC_NO_MBOX_ID;
	}

	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
			ITC_DEBUG("Failed to malloc rc for itc_create_mailbox()!");
                	return false;
		}	
	}

	new_mbox = q_dequeue(rc, itc_inst.free_mboxes_queue);
	rc->flags = ITC_OK;
	if(new_mbox == NULL)
	{
		ITC_DEBUG("Not enough available mailbox to create!");
		return ITC_NO_MBOX_ID;
	}

	if(strlen(name) > (ITC_MAX_NAME_LENGTH))
	{
		ITC_DEBUG("Requested mailbox name too long!");
		return ITC_NO_MBOX_ID;
	}
	strcpy(new_mbox->name, name);

	new_mbox->flags			= flags;
	new_mbox->p_rxq_info		= &new_mbox->rxq_info;
	new_mbox->p_rxq_info->rxq_len	= 0;
	new_mbox->p_rxq_info->is_in_rx	= 0;

	MUTEX_LOCK(&(new_mbox->p_rxq_info->rxq_mtx));

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
				ITC_DEBUG("Failed to create mailbox on trans_mechanism[%d]!", i);
				MUTEX_UNLOCK(&(new_mbox->p_rxq_info->rxq_mtx));
				return ITC_NO_MBOX_ID;
			}
		}
	}

	int ret = pthread_setspecific(itc_inst.destruct_key, new_mbox);
	if(ret != 0)
	{
		// ERROR trace is needed here
		ITC_DEBUG("Failed to pthread_setspecific, error code = %d", ret);
		MUTEX_UNLOCK(&(new_mbox->p_rxq_info->rxq_mtx));
		return ITC_NO_MBOX_ID;
	}

	/* Insert mailbox to local_locating_tree */
	if(insert_mbox_to_tree(&itc_inst.local_locating_mbox_tree, &itc_inst.local_locating_mbox_mtx, new_mbox) == false)
	{
		/* Not a big problem, will not return. User may only not be able to get precise mbox_id from name in the future for this mailbox by itc_locate_sync() request */
		ITC_DEBUG("Mailbox id 0x%08x already exists in local_locating_tree!", new_mbox->mbox_id);
	}

	my_threadlocal_mbox = new_mbox;

	MUTEX_UNLOCK(&(new_mbox->p_rxq_info->rxq_mtx));

	/* In case this process is not the itccoord process, send notification to itccoord */
	if(itc_inst.my_mbox_id_in_itccoord != (itc_inst.itccoord_mbox_id & itc_inst.itccoord_mask))
	{
		msg = itc_alloc(offsetof(struct itc_notify_coord_add_rmv_mbox, mbox_name) + strlen(name) + 1, ITC_NOTIFY_COORD_ADD_MBOX);
		msg->itc_notify_coord_add_rmv_mbox.mbox_id = new_mbox->mbox_id;
		strcpy(msg->itc_notify_coord_add_rmv_mbox.mbox_name, name);
		bool res = itc_send(&msg, itc_inst.itccoord_mbox_id, new_mbox->mbox_id, NULL);
		if(!res)
		{
			ITC_DEBUG("Failed to send notification to itccoord regarding ADD mailbox id = 0x%08x", new_mbox->mbox_id);
			itc_free(&msg);
		} else
		{
			ITC_DEBUG("Sent notification to itccoord regarding ADD mailbox id = 0x%08x", new_mbox->mbox_id);
		}
		
	}

	return new_mbox->mbox_id;
}

bool itc_delete_mailbox_zz(itc_mbox_id_t mbox_id)
{
	struct itc_mailbox* mbox;
	union itc_msg* msg;
	pthread_mutex_t* rxq_mtx;

	ITC_DEBUG("Deleting mailbox id 0x%08x", mbox_id);

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return false;
	}

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// Not allowed to delete a mailbox of other threads
		ITC_DEBUG("Not allowed to delete other thread's mailbox, mbox_id = 0x%08x", mbox_id);
		return false;
	}

	mbox = my_threadlocal_mbox;

	/* Remove mailbox from local_locating_tree */
	if(remove_mbox_from_tree(&itc_inst.local_locating_mbox_tree, &itc_inst.local_locating_mbox_mtx, mbox) == false)
	{
		/* Not a too big problem, will not return here */
		ITC_DEBUG("Failed to delete mbox_id = 0x%08x which is not found in local locating tree, something was messed up!", mbox_id);
	}

	rxq_mtx = &(mbox->p_rxq_info->rxq_mtx);
#if defined MUTEX_TRACE_TIME_UNITTEST
	struct timespec t_start;
	struct timespec t_end;
	clock_gettime(CLOCK_MONOTONIC, &t_start);
	pthread_mutex_lock(rxq_mtx);
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	unsigned long int difftime = calc_time_diff(t_start, t_end);
	if(difftime/1000000 > 10)
	{
		ITC_DEBUG("MUTEX_LOCK\t0x%08lx,\t%s:%d,\t"		\
			"time_elapsed = %lu (ms)!",			\
			(unsigned long)rxq_mtx, __FILE__, __LINE__, 	\
			difftime/1000000);
		ITC_DEBUG("MUTEX_LOCK - t_start.tv_sec = %lu!", 	\
			t_start.tv_sec);
		ITC_DEBUG("MUTEX_LOCK - t_start.tv_nsec = %lu!", 	\
			t_start.tv_nsec);
		ITC_DEBUG("MUTEX_LOCK - t_end.tv_sec = %lu!", 	\
			t_end.tv_sec);
		ITC_DEBUG("MUTEX_LOCK - t_end.tv_nsec = %lu!",	\
			t_end.tv_nsec);
	}
#elif defined MUTEX_TRACE_UNITTEST
	ITC_DEBUG("MUTEX_LOCK\t0x%08lx,\t%s:%d", 			\
			(unsigned long)rxq_mtx, __FILE__, __LINE__);	\
	pthread_mutex_lock(rxq_mtx);
#else
#endif

	/* If program is stopped over here, consider adjusting to increase sleep time for itc_receive_zz ITC_NO_WAIT case */
	ITC_DEBUG("Waiting for other job that owned rx queue mutex before deleting the mailbox!");
	int res = pthread_mutex_lock(rxq_mtx);
	if(res != 0 && res != EDEADLK)
	{
		ITC_DEBUG("pthread_mutex_lock error code = %d", res);
		return false;
	}

	mbox->mbox_state = MBOX_UNUSED;

	rc->flags = ITC_OK;
	MUTEX_UNLOCK(rxq_mtx);

	if(mbox->p_rxq_info->is_fd_created)
	{
		if(close(mbox->p_rxq_info->rxq_fd) == -1)
		{
			// ERROR trace is needed here
			ITC_DEBUG("Failed to close()!");
		}
		mbox->p_rxq_info->is_fd_created = false;
	}

	MUTEX_LOCK(rxq_mtx);

	/* Notify itccoord of my deleted mailbox */
	if(itc_inst.my_mbox_id_in_itccoord != (itc_inst.itccoord_mbox_id & itc_inst.itccoord_mask))
	{
		msg = itc_alloc(offsetof(struct itc_notify_coord_add_rmv_mbox, mbox_name) + strlen(mbox->name) + 1, ITC_NOTIFY_COORD_RMV_MBOX);
		msg->itc_notify_coord_add_rmv_mbox.mbox_id = mbox->mbox_id;
		strcpy(msg->itc_notify_coord_add_rmv_mbox.mbox_name, mbox->name);
		bool res = itc_send(&msg, itc_inst.itccoord_mbox_id, mbox->mbox_id, NULL);
		if(!res)
		{
			ITC_DEBUG("Failed to send notification to itccoord regarding RMV mailbox id = 0x%08x", mbox->mbox_id);
			itc_free(&msg);
		} else
		{
			ITC_DEBUG("Sent notification to itccoord about my demise mbox_id = 0x%08x", mbox->mbox_id);
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
				ITC_DEBUG("Failed to delete mailbox on trans_mechanism[%d]!", i);
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
		ITC_DEBUG("Failed to q_enqueue!");
		return false;
	}

	MUTEX_UNLOCK(rxq_mtx);

	my_threadlocal_mbox = NULL;

	ITC_DEBUG("Deleted thread-local mailbox 0x%08x", mbox_id);
	return true;
}

bool itc_send_zz(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace)
{
	struct itc_message* message;
	struct itc_mailbox* to_mbox;



	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return false;
	}

	if(to == my_threadlocal_mbox->mbox_id && (namespace == NULL || (strcmp(namespace, itc_inst.namespace) == 0)))
	{
		ITC_DEBUG("Not allowed to send messages to myself, which causes deadlock, from = 0x%08x, to = 0x%08x", from, to);
		return false;
	}

	if(from != ITC_MY_MBOX_ID && from != my_threadlocal_mbox->mbox_id)
	{
		// Not allowed to use mailboxes of other threads to send messages
		ITC_DEBUG("Not allowed to use other thread's mailbox to send messages, mbox_id = 0x%08x", from);
		return false;
	}

	if(msg == NULL || *msg == NULL)
	{
		ITC_DEBUG("The sending message is NULL!");
		return false;
	}

	/* If namespace is specified and it differs from our namespace, forward the message to itcgw to send it outside */
	if(namespace != NULL && (strcmp(namespace, itc_inst.namespace) != 0))
	{
		ITC_DEBUG("Prepare to send message outside host, namespace = %s, from 0x%08x to 0x%08x, msgno = 0x%08x", namespace, from, to, (*msg)->msgno);
		if(handle_forward_itc_msg_to_itcgw(msg, to, namespace) == false)
		{
			ITC_DEBUG("Failed to send message to itcgw!");
			return false;
		}

		return true;
	}

	/* Otherwise send message locally within our host */
	ITC_DEBUG("Prepare to send message from 0x%08x to 0x%08x, msgno = 0x%08x", from, to, (*msg)->msgno);

	message = CONVERT_TO_MESSAGE(*msg);
	message->sender = my_threadlocal_mbox->mbox_id;
	message->receiver = to;

	rc->flags = ITC_OK;
	to_mbox = find_mbox(to);
	if(to_mbox != NULL)
	{
		// Local mailbox
		if(to_mbox->mbox_state != MBOX_INUSE)
		{
			// Send a message to a non-active mailbox
			ITC_DEBUG("Sending message to a non-active mailbox!");
			return false;
		}
		MUTEX_LOCK(&(to_mbox->rxq_info.rxq_mtx));
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
					MUTEX_UNLOCK(&(to_mbox->p_rxq_info->rxq_mtx));
				}
			} else
			{
				ITC_DEBUG("Sent successfully on trans_mechanism[%u]!", idx);
				break;
			}
		}
	}

	if(idx == ITC_NUM_TRANS)
	{
		if(to_mbox != NULL)
		{
			MUTEX_UNLOCK(&(to_mbox->p_rxq_info->rxq_mtx));
		}
		// ERROR trace is needed here. Failed to send the message on all mechanisms
		ITC_DEBUG("Failed to send message by all transport mechanisms!");
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
				ITC_DEBUG("Failed to write()!");
			}
		}

		to_mbox->p_rxq_info->rxq_len++;
		pthread_cond_signal(&(to_mbox->p_rxq_info->rxq_cond));
		MUTEX_UNLOCK(&(to_mbox->p_rxq_info->rxq_mtx));

		pthread_setcancelstate(saved_cancel_state, NULL);
		ITC_DEBUG("Notify receiver about sent messages!");
	}

	*msg = NULL;

	return true;
}

union itc_msg *itc_receive_zz(int32_t tmo)
{
	struct itc_message* message = NULL;
	struct itc_mailbox* mbox;
	struct timespec ts;

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return NULL;
	}

	mbox = my_threadlocal_mbox;

	if(tmo != ITC_WAIT_FOREVER && tmo != ITC_NO_WAIT && tmo > 0)
	{
		calc_abs_time(&ts, tmo);
	}

	rc->flags = ITC_OK;
	do
	{
		MUTEX_LOCK(&(mbox->p_rxq_info->rxq_mtx));

		mbox->p_rxq_info->is_in_rx = true;
		
		for(uint32_t i = 0; i < ITC_NUM_TRANS; i++)
		{
			if(trans_mechanisms[i].itci_trans_receive != NULL)
			{
				rc->flags = ITC_OK;
				message = trans_mechanisms[i].itci_trans_receive(rc, mbox);
				if(message != NULL)
				{
					ITC_DEBUG("Received a message on trans_mechanisms[%u]!", i);
					break;
				}
			}
		}

		if(message == NULL)
		{
			if(tmo == ITC_NO_WAIT)
			{
				/* If nothing in rx queue, return immediately */
				// ITC_DEBUG("No message in rx queue, return!"); SPAM
				mbox->p_rxq_info->is_in_rx = false;
				MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
				/* Sleep a little bit to avoid EDEADLK when deleting the mailbox in case of using ITC_NO_WAIT in while true loop
				*  Hope this will not affect much to the latency, but it's safe and worth doing this */
				sleep(0.01);
				break;
			} else if(tmo == ITC_WAIT_FOREVER)
			{
				/* Wait undefinitely until we receive something from rx queue */
				ITC_DEBUG("Waiting for incoming messages...!");
				int ret = pthread_cond_wait(&(mbox->p_rxq_info->rxq_cond), &(mbox->p_rxq_info->rxq_mtx));
				if(ret != 0)
				{
					// ERROR trace is needed here
					ITC_DEBUG("pthread_cond_wait error code = %d", ret);
					return NULL;
				}
			} else
			{
				int ret = pthread_cond_timedwait(&(mbox->p_rxq_info->rxq_cond), &(mbox->p_rxq_info->rxq_mtx), &ts);
				if(ret == ETIMEDOUT)
				{
					ITC_DEBUG("Timeout when expecting message, timeout = %u ms!", tmo);
					mbox->p_rxq_info->is_in_rx = false;
					MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
					break;
				} else if(ret != 0)
				{
					// ERROR trace is needed here
					ITC_DEBUG("pthread_cond_timedwait error code = %d", ret);
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
					ITC_DEBUG("Failed to read()!");
					return NULL;
				}
			}
			
			
		}

		mbox->p_rxq_info->is_in_rx = false;
		
		MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
	} while(message == NULL);

	return (union itc_msg*)((message == NULL) ? NULL : CONVERT_TO_MSG(message));
}

itc_mbox_id_t itc_sender_zz(union itc_msg *msg)
{
	struct itc_message* message;

	if(msg == NULL)
	{
		ITC_DEBUG("Could not get sender from a NULL message!");
		return ITC_NO_MBOX_ID;
	}

	message = CONVERT_TO_MESSAGE(msg);
	return message->sender;
}

itc_mbox_id_t itc_receiver_zz(union itc_msg *msg)
{
	struct itc_message* message;

	if(msg == NULL)
	{
		ITC_DEBUG("Could not get receiver from a NULL message!");
		return ITC_NO_MBOX_ID;
	}

	message = CONVERT_TO_MESSAGE(msg);
	return message->receiver;
}

size_t itc_size_zz(union itc_msg *msg)
{
	struct itc_message* message;

	if(msg == NULL)
	{
		ITC_DEBUG("Could not get size from a NULL message!");
		return 0;
	}

	message = CONVERT_TO_MESSAGE(msg);
	return message->size;
}

itc_mbox_id_t itc_current_mbox_zz()
{
	if(my_threadlocal_mbox != NULL)
	{
		ITC_DEBUG("This thread has mailbox with id = 0x%08x", my_threadlocal_mbox->mbox_id);
		return my_threadlocal_mbox->mbox_id;
	}

	ITC_DEBUG("This thread has no mailbox!");
	return ITC_NO_MBOX_ID;
}

int itc_get_fd_zz(itc_mbox_id_t mbox_id)
{
	struct itc_mailbox* mbox;

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return -1;
	}

	mbox = my_threadlocal_mbox;

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// "mbox_id" mailbox is not from this thread
		ITC_DEBUG("Mailbox not owned by this thread, mbox_id = 0x%08x, this thread's mbox_id = 0x%08x", mbox_id, mbox->mbox_id);
		return -1;
	}

	rc->flags = ITC_OK;
	MUTEX_LOCK(&(mbox->p_rxq_info->rxq_mtx));

	uint64_t one = 1;
	if(!mbox->p_rxq_info->is_fd_created)
	{
		mbox->p_rxq_info->rxq_fd = eventfd(0, 0);
		if(mbox->p_rxq_info->rxq_fd == -1)
		{
			ITC_DEBUG("Failed to eventfd()!");
			MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
			return -1;
		}
		mbox->p_rxq_info->is_fd_created = true;
		if(mbox->p_rxq_info->rxq_len != 0)
		{
			if(write(mbox->p_rxq_info->rxq_fd, &one, 8) < 0)
			{
				ITC_DEBUG("Failed to write()!");
				MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
				return -1;
			}
		}
	}

	MUTEX_UNLOCK(&(mbox->p_rxq_info->rxq_mtx));
	return mbox->p_rxq_info->rxq_fd;
}

bool itc_get_name_zz(itc_mbox_id_t mbox_id, char *name)
{
	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return false;
	}

	if(mbox_id != my_threadlocal_mbox->mbox_id)
	{
		// "mbox_id" mailbox is not from this thread
		ITC_DEBUG("Mailbox not owned by this thread, mbox_id = 0x%08x, this thread's mbox_id = 0x%08x", mbox_id, my_threadlocal_mbox->mbox_id);
		return false;
	}

	strcpy(name, my_threadlocal_mbox->name);
	return true;
}

itc_mbox_id_t itc_locate_sync_zz(int32_t timeout, const char *name, bool find_only_internal, bool *is_external, char *namespace)
{
	itc_mbox_id_t mbox_id = ITC_NO_MBOX_ID;
	struct itc_mailbox *mbox;
	union itc_msg *msg;
	pid_t pid;

	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return ITC_NO_MBOX_ID;
	}

	/* First search for local mailboxes in the current process. */
	mbox = locate_local_mbox(name);
	if(mbox != NULL)
	{
		ITC_DEBUG("Mailbox \"%s\" was found in this process pid = %d!", name, itc_inst.pid);
		if(!find_only_internal)
		{
			*is_external = false;
			strcpy(namespace, "");
		}
		return mbox->mbox_id;
	}

	/* If cannot find locally, send a message ITC_LOCATE_MBOX_SYNC_REQ to itc_coord asking for seeking across processes. */
	ITC_DEBUG("Mailbox %s not found in local process, send ITC_LOCATE_MBOX_SYNC_REQUEST to itccoord!", name);
	msg = itc_alloc(offsetof(struct itc_locate_mbox_sync_request, mbox_name) + strlen(name) + 1, ITC_LOCATE_MBOX_SYNC_REQUEST);
	msg->itc_locate_mbox_sync_request.from_mbox = my_threadlocal_mbox->mbox_id;
	msg->itc_locate_mbox_sync_request.timeout = timeout;
	msg->itc_locate_mbox_sync_request.find_only_internal = find_only_internal;
	strcpy(msg->itc_locate_mbox_sync_request.mbox_name, name);
	if(itc_send(&msg, itc_inst.itccoord_mbox_id, ITC_MY_MBOX_ID, NULL) == false)
	{
		ITC_DEBUG("Failed to send ITC_LOCATE_MBOX_SYNC_REQUEST to itccoord!");
		itc_free(&msg);
		return ITC_NO_MBOX_ID;
	}

	msg = itc_receive(timeout);
	if(msg == NULL)
	{
		ITC_DEBUG("Failed to receive ITC_LOCATE_MBOX_SYNC_REPLY from itccoord even after %d ms!", timeout);
		return false;
	} else if(msg->msgno != ITC_LOCATE_MBOX_SYNC_REPLY)
	{
		ITC_DEBUG("Received unknown message 0x%08x, expecting ITC_LOCATE_MBOX_SYNC_REPLY!", msg->msgno);
		itc_free(&msg);
		return ITC_NO_MBOX_ID;
	}

	mbox_id = msg->itc_locate_mbox_sync_reply.mbox_id;

	if(!find_only_internal)
	{
		*is_external = msg->itc_locate_mbox_sync_reply.is_external;
		strcpy(namespace, msg->itc_locate_mbox_sync_reply.namespace);
	}

	pid = msg->itc_locate_mbox_sync_reply.pid;
	ITC_DEBUG("Locating mailbox \"%s\" successfully with mbox_id = 0x%08x from pid = %d!", name, mbox_id, pid);
	itc_free(&msg);
	return mbox_id;
}

bool itc_get_namespace_zz(int32_t timeout, char *name)
{
	if(itc_inst.mboxes == NULL || my_threadlocal_mbox == NULL)
	{
		// Not initialized yet
		// ERROR trace is needed
		ITC_DEBUG("Not initialized yet!");
		return false;
	}

	if(strcmp(itc_inst.namespace, "") != 0)
	{
		ITC_DEBUG("Namespace already available \"%s\"", itc_inst.namespace);
		strcpy(name, itc_inst.namespace);
		return true;
	}

	/* We will need to retrieve our host's namespace from itc gateway */
	union itc_msg *req;
	req = itc_alloc(sizeof(struct itc_get_namespace_request), ITC_GET_NAMESPACE_REQUEST);
	req->itc_get_namespace_request.mbox_id = my_threadlocal_mbox->mbox_id;

	/* Instead of sending get namespace request to TCP client mailbox of itc gateway, we will send it to UDP mailbox to secure performance for TCP client thread */
	itc_mbox_id_t itcgw_mboxid = itc_locate_sync(timeout, ITC_GATEWAY_MBOX_UDP_NAME2, 1, NULL, NULL); // TEST ONLY
	if(itcgw_mboxid == ITC_NO_MBOX_ID)
	{
		ITC_DEBUG("Failed to locate mailbox %s even after %d ms!", ITC_GATEWAY_MBOX_UDP_NAME2, timeout); // TEST ONLY
		itc_free(&req);
		return false;
	}

	if(itc_send(&req, itcgw_mboxid, ITC_MY_MBOX_ID, NULL) == false)
	{
		ITC_DEBUG("Failed to send message to mailbox %s!", ITC_GATEWAY_MBOX_UDP_NAME2); // TEST ONLY
		itc_free(&req);
		return false;
	}

	union itc_msg *rep;
	rep = itc_receive(timeout);

	if(rep == NULL)
	{
		ITC_DEBUG("Failed to retrieve namespace from itc gateway even after %d ms!", timeout);
		return false;
	} else if(rep->msgno != ITC_GET_NAMESPACE_REPLY)
	{
		ITC_DEBUG("Not receiving ITC_GET_NAMESPACE_REPLY as expected, msgno = 0x%08x", rep->msgno);
		itc_free(&rep);
		return false;
	}

	strcpy(itc_inst.namespace, rep->itc_get_namespace_reply.namespace);
	strcpy(name, itc_inst.namespace);
	itc_free(&rep);
	ITC_DEBUG("Retrieve namespace \"%s\" from itc gateway successfully!", itc_inst.namespace);
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
		ITC_DEBUG("pthread_key_delete error code = %d", ret);
		return;
	}

	// if(itc_inst.name_space)
	// {
	// 	free(itc_inst.name_space);
	// }

	tdestroy(itc_inst.local_locating_mbox_tree, do_nothing);

	free(itc_inst.mboxes);
	free(rc);
	rc = NULL;
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

	ITC_DEBUG("Thread-local mailbox destructor called by tid = %d, mbox_id = 0x%08x", mbox->tid, mbox->mbox_id);
	if(itc_inst.mboxes != NULL && mbox->mbox_state == MBOX_INUSE)
	{
		if(my_threadlocal_mbox == mbox)
		{
			ITC_DEBUG("Deleting mailbox with mbox_id = 0x%08x", mbox->mbox_id);
			itc_delete_mailbox(mbox->mbox_id);
		}
	}

	free(rc);
	rc = NULL;
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

	ITC_DEBUG("Mailbox not belong to this process, mbox_id = 0x%08x", mbox_id);
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

unsigned long int calc_time_diff(struct timespec t_start, struct timespec t_end)
{
	unsigned long int diff = 0;

	if(t_end.tv_nsec < t_start.tv_nsec)
	{
		diff = (t_end.tv_sec - t_start.tv_sec)*1000000000 - (t_start.tv_nsec - t_end.tv_nsec);
	} else
	{
		diff = (t_end.tv_sec - t_start.tv_sec)*1000000000 + (t_end.tv_nsec - t_start.tv_nsec);
	}

	return diff; 
}

static struct itc_mailbox *locate_local_mbox(const char *name)
{
	struct itc_mailbox **iter, *mbox;

	MUTEX_LOCK(&itc_inst.local_locating_mbox_mtx);

	iter = tfind(name, &itc_inst.local_locating_mbox_tree, mbox_name_cmpfunc);
	mbox = iter ? *iter : NULL;

	MUTEX_UNLOCK(&itc_inst.local_locating_mbox_mtx);

	return mbox;
}

static int mbox_name_cmpfunc(const void *pa, const void *pb)
{
	const char *name = pa;
	const struct itc_mailbox *mbox = pb;

	return strcmp(name, mbox->name);
}

static void do_nothing(void *a)
{
	(void)a;
}

static bool remove_mbox_from_tree(void **tree, pthread_mutex_t *tree_mtx, struct itc_mailbox *mbox)
{
	struct itc_mailbox **iter;
	bool found = true;

	MUTEX_LOCK(tree_mtx);

	iter = tfind(mbox, tree, mbox_name_cmpfunc2);
	if(iter == NULL)
	{
		found = false;
	} else
	{
		tdelete(mbox, tree, mbox_name_cmpfunc2);
	}

	MUTEX_UNLOCK(tree_mtx);

	return found;
}

static int mbox_name_cmpfunc2(const void *pa, const void *pb)
{
	const struct itc_mailbox *mbox1 = pa;
	const struct itc_mailbox *mbox2 = pb;

	return strcmp(mbox1->name, mbox2->name);
}

static bool insert_mbox_to_tree(void **tree, pthread_mutex_t *tree_mtx, struct itc_mailbox *mbox)
{
	struct itc_mailbox **iter;
	bool found = false;

	MUTEX_LOCK(tree_mtx);

	iter = tfind(mbox, tree, mbox_name_cmpfunc2);
	if(iter != NULL)
	{
		found = true;
	} else
	{
		tsearch(mbox, tree, mbox_name_cmpfunc2);
	}

	MUTEX_UNLOCK(tree_mtx);

	/* This function will fail when mbox is already exist in the tree. We expect it should not be in tree instead. */
	return !found;
}

static bool handle_forward_itc_msg_to_itcgw(union itc_msg **msg, itc_mbox_id_t to, char *namespace)
{
	struct itc_message* message;

	message = CONVERT_TO_MESSAGE(*msg);
	message->sender 	= my_threadlocal_mbox->mbox_id;
	message->receiver 	= to;

	size_t payload_len = message->size + ITC_HEADER_SIZE; // No need to carry the ENDPOINT
	union itc_msg *req;
	req = itc_alloc(offsetof(struct itc_fwd_data_to_itcgws, payload) + payload_len, ITC_FWD_DATA_TO_ITCGWS);

	strcpy(req->itc_fwd_data_to_itcgws.to_namespace, namespace);
	req->itc_fwd_data_to_itcgws.payload_length = payload_len;
	memcpy(req->itc_fwd_data_to_itcgws.payload, message, payload_len);

	int32_t timeout = 1000; // Wait max 1000 ms for locating mailbox name
	itc_mbox_id_t itcgw_mboxid = itc_locate_sync(timeout, ITC_GATEWAY_MBOX_TCP_CLI_NAME2, 1, NULL, NULL); // TEST ONLY
	if(itcgw_mboxid == ITC_NO_MBOX_ID)
	{
		ITC_DEBUG("Failed to locate mailbox %s even after %d ms!", ITC_GATEWAY_MBOX_TCP_CLI_NAME2, timeout); // TEST ONLY
		itc_free(&req);
		return false;
	}

	if(itc_send(&req, itcgw_mboxid, ITC_MY_MBOX_ID, NULL) == false)
	{
		ITC_DEBUG("Failed to send message to mailbox %s!", ITC_GATEWAY_MBOX_TCP_CLI_NAME2); // TEST ONLY
		itc_free(&req);
		return false;
	}

	return true;
}

void ITC_INFO_ZZ(const char *file, int line, const char *format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	char fileline[40];
	snprintf(fileline, 40, "%s:%d", file, line);
	fprintf(stdout, "%-10s %-40s msg: %-s\n", "INFO:", fileline, buffer);
	fflush(stdout);
}

void ITC_ERROR_ZZ(const char *file, int line, const char *format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	char fileline[40];
	snprintf(fileline, 40, "%s:%d", file, line);
	fprintf(stdout, "%-10s %-40s msg: %-s\n", "ERROR:", fileline, buffer);
	fflush(stdout);
}

void ITC_ABN_ZZ(const char *file, int line, const char *format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	char fileline[40];
	snprintf(fileline, 40, "%s:%d", file, line);
	fprintf(stdout, "%-10s %-40s msg: %-s\n", "ABN:", fileline, buffer);
	fflush(stdout);
}

void ITC_DEBUG_ZZ(const char *file, int line, const char *format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	char fileline[40];
	snprintf(fileline, 40, "%s:%d", file, line);
	fprintf(stdout, "%-10s %-40s msg: %-s\n", "DEBUG:", fileline, buffer);
	fflush(stdout);
}