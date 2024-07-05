#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <search.h>
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

#include <sys/ipc.h>
#include <sys/shm.h> 
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"
#include "itc_threadmanager.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                    INTERNAL TYPES IN SYSVSHM-ATOR                   *****
*******************************************************************************/
#define SYSVSHM_PAGE_SIZE		4096
#define SYSVSHM_SLOT_HEADER_SIZE	32
#define MAX_SYSVSHM_SLOTS		70
#define SYSVSHM_STATIC_ALLOC_PAGES	25
#define NUM_SLOTS_POOL_480		16
#define NUM_SLOTS_POOL_992		8
#define NUM_SLOTS_POOL_2016		8
#define NUM_SLOTS_POOL_4064		4
#define NUM_SLOTS_POOL_16352		3
#define NUM_SLOTS_POOL_UNLIMIT		1

enum sysvshm_pool_type_e {
	POOL_96 = 0, // 1 page (same page as metadata) - ((SYSVSHM_PAGE_SIZE - (sizeof(sysvshm_metadata_t)/128 + 1)*128) / 128) slots (< 32 slots)
	POOL_480, // 2 pages, 16 slots
	POOL_992, // 2 pages, 8 slots
	POOL_2016, // 4 pages, 8 slots
	POOL_4064, // 4 pages, 4 slots
	POOL_16352, // 12 pages, 3 slots -> statically allocated pages in total = 25 pages ~ 100KB
	POOL_UNLIMIT, // n pages (size > 16352 bytes), only 1 slot at a time -> dynamically allocated pages
	NUM_POOLS
};

union semattr_un {
	int val;
	struct semid_ds *buf;
	unsigned short array [1];
};

struct sysvshm_pool_slot {
	int					whichpool;
	int					whichslot;
};

struct sysvshm_slot_info_t {
	struct sysvshm_pool_slot		next_slot;
	unsigned char				pool_type;
	bool					is_in_use;
};

struct sysvshm_metadata_t {
	uint16_t				num_pages;
	uint16_t				num_unlimit_pages;
	struct sysvshm_pool_slot		head; // to let receiver dequeue messages
	struct sysvshm_pool_slot		tail; // to let sender enqueue messages

	struct sysvshm_slot_info_t		slot_unlimit;
};

struct sysvshm_contactlist {
	itc_mbox_id_t				mbox_id_in_itccoord;
	int					sysvshm_id;
	struct sysvshm_metadata_t		*metadata;
	// Binary semaphore indicating that only one process can access shared memory at a time
	int					m_sem_mutex;
	// Binary semaphore used by sender to trigger a notification towards receiver telling them that a new message has been enqueued
	// So receiver will block on sem_wait(m_sem_notify) while sender will call sem_post(m_sem_notify)
	int					m_sem_notify;
	// Counting semaphore used by receiver to indicate that a message has been dequeued and queue buffer has new slot for another message
	int					m_sem_slot;

	int					unlimit_sysvshm_id;
	void					*unlimit_shm_ptr;
};

struct sysvshm_instance {
	itc_mbox_id_t           	my_mbox_id_in_itccoord;
	itc_mbox_id_t           	itccoord_mask;
        itc_mbox_id_t           	itccoord_shift;

	int				my_shmid;
	struct sysvshm_metadata_t	*my_shm_ptr;

	int				my_unlimit_shmid;
	void				*my_unlimit_shm_ptr;

	int				my_sem_mutex;
	int				my_sem_notify;
	int				my_sem_slot;
	union semattr_un		sem_attr;

	int				is_initialized;
	int				is_terminated;

	itc_mbox_id_t			my_mbox_id;
	pthread_mutex_t			thread_mtx;
	pthread_key_t			destruct_key;

	unsigned char			num_slots[NUM_POOLS]; // how many slots in each pool
	int16_t				slot_sizes[NUM_POOLS]; // get slot's size of a specific pool, for example 128, 512,... and -1 for unlimited pool
	unsigned long			pool_offset[NUM_POOLS]; // get slot's size of a specific pool, for example 128, 512,... and -1 for unlimited pool

	struct sysvshm_contactlist	sysvshm_cl[MAX_SUPPORTED_PROCESSES];
};


/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct sysvshm_instance sysvshm_inst; // One instance per a process, multiple threads all use this one.



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void init_sysvshm(struct result_code* rc);
static void init_sysvshm_semaphores(struct result_code* rc);
static void remove_sysvshm();
static void remove_sysvshm_semaphores();
static void init_sysvshm_rx_thread(struct result_code* rc);
static void init_sysvshm_mempool(struct sysvshm_metadata_t *shm_ptr);
static void release_sysvshm_resources(struct result_code* rc);
static void generate_msqfile(struct result_code* rc);

static struct sysvshm_contactlist* get_sysvshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static struct sysvshm_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static void add_sysvshm_cl(struct result_code* rc, struct sysvshm_contactlist* cl, itc_mbox_id_t mbox_id);
static void remove_sysvshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id);

static void handle_received_message(struct itc_message *p_message);
static void rxthread_destructor(void* data);

static void init_unilimit_sysvshm(struct result_code* rc, uint16_t num_unlimit_pages, struct sysvshm_contactlist* cl);
static void get_my_unlimit_shm(uint16_t num_unlimit_pages);
static void release_sysvshm_contactlist();



/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void sysvshm_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags);

static void sysvshm_exit(struct result_code* rc);

static void sysvshm_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static void* sysvshm_rx_thread(void *data);

struct itci_transport_apis sysvshm_trans_apis = { NULL,
                                            	sysvshm_init,
                                            	sysvshm_exit,
                                            	NULL,
                                            	NULL,
                                            	sysvshm_send,
                                            	NULL,
                                            	NULL,
                                            	NULL };





/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
void sysvshm_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;

	if(sysvshm_inst.is_initialized == 1)
	{
		if(flags & ITC_FLAGS_FORCE_REINIT)
		{
			TPT_TRACE(TRACE_INFO, "Force re-initializing!");
			release_sysvshm_resources(rc);
			if(rc != ITC_OK)
			{
				TPT_TRACE(TRACE_ERROR, "Failed to release_sysvshm_resources!");
				return;
			}
		} else
		{
			TPT_TRACE(TRACE_INFO, "Already initialized!");
			rc->flags |= ITC_ALREADY_INIT;
			return;
		}
	}

	generate_msqfile(rc);
	if(rc->flags != ITC_OK)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to generate sysvshm file!");
		return;
	}

	// Calculate itccoord_shift value
	int tmp_shift, tmp_mask;
	tmp_shift = 0;
	tmp_mask = itccoord_mask;
	while(!(tmp_mask & 0x1))
	{
		tmp_mask = tmp_mask >> 1;
		tmp_shift++; // Should be 20 for current design
	}

	sysvshm_inst.my_shmid			= -1;
	sysvshm_inst.itccoord_mask 		= itccoord_mask;
	sysvshm_inst.itccoord_shift		= tmp_shift;
	sysvshm_inst.my_mbox_id_in_itccoord 	= my_mbox_id_in_itccoord;

	sysvshm_inst.num_slots[POOL_96]		= (SYSVSHM_PAGE_SIZE / 128) - (sizeof(struct sysvshm_metadata_t)/128 + 1);
	sysvshm_inst.num_slots[POOL_480]	= NUM_SLOTS_POOL_480;
	sysvshm_inst.num_slots[POOL_992]	= NUM_SLOTS_POOL_992;
	sysvshm_inst.num_slots[POOL_2016]	= NUM_SLOTS_POOL_2016;
	sysvshm_inst.num_slots[POOL_4064]	= NUM_SLOTS_POOL_4064;
	sysvshm_inst.num_slots[POOL_16352]	= NUM_SLOTS_POOL_16352;
	sysvshm_inst.num_slots[POOL_UNLIMIT]	= NUM_SLOTS_POOL_UNLIMIT;

	sysvshm_inst.slot_sizes[POOL_96]	= 128;
	sysvshm_inst.slot_sizes[POOL_480]	= 512;
	sysvshm_inst.slot_sizes[POOL_992]	= 1024;
	sysvshm_inst.slot_sizes[POOL_2016]	= 2048;
	sysvshm_inst.slot_sizes[POOL_4064]	= 4096;
	sysvshm_inst.slot_sizes[POOL_16352]	= 16384;
	sysvshm_inst.slot_sizes[POOL_UNLIMIT]	= -1;

	sysvshm_inst.pool_offset[POOL_96]		= (sizeof(struct sysvshm_metadata_t)/128 + 1)*128;
	sysvshm_inst.pool_offset[POOL_480]		= SYSVSHM_PAGE_SIZE;
	sysvshm_inst.pool_offset[POOL_992]		= 3 * SYSVSHM_PAGE_SIZE;
	sysvshm_inst.pool_offset[POOL_2016]		= 5 * SYSVSHM_PAGE_SIZE;
	sysvshm_inst.pool_offset[POOL_4064]		= 9 * SYSVSHM_PAGE_SIZE;
	sysvshm_inst.pool_offset[POOL_16352]		= 13 * SYSVSHM_PAGE_SIZE;
	sysvshm_inst.pool_offset[POOL_UNLIMIT]		= offsetof(struct sysvshm_metadata_t, slot_unlimit);

	init_sysvshm_semaphores(rc);

	init_sysvshm(rc);
	
	init_sysvshm_rx_thread(rc);

	sysvshm_inst.is_initialized = 1;
}

static void sysvshm_exit(struct result_code* rc)
{
	if(!sysvshm_inst.is_initialized)
	{
		TPT_TRACE(TRACE_ABN, "Not initialized yet!");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	release_sysvshm_resources(rc);
}

static void sysvshm_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to)
{
	struct sysvshm_contactlist* cl;

	if(!sysvshm_inst.is_initialized)
	{
		TPT_TRACE(TRACE_ABN, "Not initialized yet!");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	// Choose which POOL should be used
	uint16_t num_unlimit_pages = 0;
	int16_t whichpool = 0;
	for(; whichpool < NUM_POOLS; ++whichpool)
	{
		// 1 byte for ENDPOINT and 32 bytes for slot's header
		if(sysvshm_inst.slot_sizes[whichpool] > 0 && (message->size + ITC_HEADER_SIZE + 1 + 32) <= (uint32_t)sysvshm_inst.slot_sizes[whichpool])
		{
			break;
		}
	}

	if(whichpool == NUM_POOLS)
	{
		whichpool = POOL_UNLIMIT;
		num_unlimit_pages = ((message->size + ITC_HEADER_SIZE + 1) / SYSVSHM_PAGE_SIZE) + 1;
	}

	cl = get_sysvshm_cl(rc, to);
	if(cl == NULL || cl->mbox_id_in_itccoord == 0)
	{
		TPT_TRACE(TRACE_ABN, "Receiver side not initialised SYSV shared memory yet!");
		rc->flags &= ~ITC_SYSCALL_ERROR; // The receiver side has not initialised message queue yet
		rc->flags |= ITC_QUEUE_NULL; // So remove unecessary syscall error, return an ITC_QUEUE_NULL warning instead. This is not an ERROR at all!
		/* If send failed, users have to self-free the message. ITC system only free messages when send successfully */
		return;
	}

	struct sembuf asem[1];
	asem[0].sem_num = 0;
	asem[0].sem_flg = 0;

	// Get into a line before allowed inserting an itc message into receiver shared memory's queue
	// Have to wait if receiver queue has been fulled with MAX_SYSVSHM_SLOTS messages
	asem[0].sem_op = -1;
	if(semop(cl->m_sem_slot, asem, 1) == -1)
	{
		if(errno != EIDRM)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(cl->m_sem_slot), mbox_id_in_itccoord = 0x%08x, errno = %d!", cl->mbox_id_in_itccoord, errno);
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		TPT_TRACE(TRACE_ERROR, "Receiver sysvshm has been disconnected, reconnecting to them!");
		remove_sysvshm_cl(rc, to);
		add_sysvshm_cl(rc, cl, to);
		if(cl->mbox_id_in_itccoord == 0)
		{
			TPT_TRACE(TRACE_ERROR, "Add contact list again failed, receiver process not online!");
			return;
		}
	}

	// Accquire mutex lock to access receiver's shared memory resources
	asem[0].sem_op = -1;
	if(semop(cl->m_sem_mutex, asem, 1) == -1)
	{
		if(errno != EIDRM)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(cl->m_sem_mutex), mbox_id_in_itccoord = 0x%08x, errno = %d!", cl->mbox_id_in_itccoord, errno);
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		TPT_TRACE(TRACE_ERROR, "Receiver sysvshm has been disconnected, reconnecting to them!");
		remove_sysvshm_cl(rc, to);
		add_sysvshm_cl(rc, cl, to);
		if(cl->mbox_id_in_itccoord == 0)
		{
			TPT_TRACE(TRACE_ERROR, "Add contact list again failed, receiver process not online!");
			return;
		}
	}

	// Critical section
	struct sysvshm_slot_info_t *new_slot;
	unsigned char new_slot_index = 0;
	if(whichpool == POOL_UNLIMIT && num_unlimit_pages > (sysvshm_inst.slot_sizes[POOL_16352] / SYSVSHM_PAGE_SIZE))
	{
		init_unilimit_sysvshm(rc, num_unlimit_pages, cl);

		cl->metadata->num_unlimit_pages = num_unlimit_pages;
		cl->metadata->num_pages = SYSVSHM_STATIC_ALLOC_PAGES + num_unlimit_pages;

		new_slot = (struct sysvshm_slot_info_t *)((unsigned long)cl->metadata + sysvshm_inst.pool_offset[whichpool]);
		new_slot->next_slot.whichpool = -1;
		new_slot->next_slot.whichslot = -1;
		new_slot->pool_type = POOL_UNLIMIT;
	} else
	{
		// Shopping for a slot in whichpool
		struct sysvshm_slot_info_t *slot = (struct sysvshm_slot_info_t *)((unsigned long)cl->metadata + sysvshm_inst.pool_offset[whichpool]);
		for(; new_slot_index < sysvshm_inst.num_slots[whichpool]; ++new_slot_index)
		{
			if(slot->is_in_use == false && slot->next_slot.whichpool == -1 && slot->next_slot.whichslot == -1)
			{
				new_slot = slot;
				break;
			}

			// Move to next slot in whichpool
			slot = (struct sysvshm_slot_info_t *)((unsigned long)slot + sysvshm_inst.slot_sizes[whichpool]);
		}

		if(new_slot_index == sysvshm_inst.num_slots[whichpool])
		{
			TPT_TRACE(TRACE_ERROR, "No more slot available for this pool = %d, limit at %u slots!", whichpool, sysvshm_inst.num_slots[whichpool]);
			return;
		}
	}

	if(cl->metadata->tail.whichpool == -1 && cl->metadata->tail.whichslot == -1)
	{
		// TPT_TRACE(TRACE_INFO, "SYSV shared memory queue is empty, add a new one!"); // TBD
		cl->metadata->head.whichpool = whichpool;
		cl->metadata->head.whichslot = new_slot_index;
		cl->metadata->tail.whichpool = whichpool;
		cl->metadata->tail.whichslot = new_slot_index;
	} else
	{
		unsigned long pool_offset = sysvshm_inst.pool_offset[cl->metadata->tail.whichpool];
		unsigned long slot_offset = sysvshm_inst.slot_sizes[cl->metadata->tail.whichpool]*cl->metadata->tail.whichslot;
		struct sysvshm_slot_info_t *tail_node = (struct sysvshm_slot_info_t *)((unsigned long)cl->metadata + pool_offset + slot_offset);
		tail_node->next_slot.whichpool = whichpool;
		tail_node->next_slot.whichslot = new_slot_index;
		cl->metadata->tail.whichpool = whichpool;
		cl->metadata->tail.whichslot = new_slot_index;
	}

	new_slot->is_in_use = true;

	if(whichpool == POOL_UNLIMIT && num_unlimit_pages > (sysvshm_inst.slot_sizes[POOL_16352] / SYSVSHM_PAGE_SIZE))
	{
		memcpy((void *)((unsigned long)cl->unlimit_shm_ptr), message, message->size + ITC_HEADER_SIZE + 1);

		// Unmmap unlimited slot to save space for our sender's virtual address space
		if (shmdt((void *)cl->unlimit_shm_ptr) == -1) {
			TPT_TRACE(TRACE_ERROR, "Failed to shmdt, errno = %d!", errno);
		}

		cl->unlimit_sysvshm_id = 0;
		cl->unlimit_shm_ptr = NULL;
	} else
	{
		memcpy((void *)((unsigned long)new_slot + SYSVSHM_SLOT_HEADER_SIZE), message, message->size + ITC_HEADER_SIZE + 1);
	}

	// Release mutex lock
	asem[0].sem_op = 1;
	if(semop(cl->m_sem_mutex, asem, 1) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_post(cl->m_sem_mutex), errno = %d!", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// Notify receiver about incoming message
	asem[0].sem_op = 1;
	if(semop(cl->m_sem_notify, asem, 1) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_post(my_sem_slot), errno = %d!", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
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

static void* sysvshm_rx_thread(void *data)
{
	(void)data;

	char itc_mbox_name[30];

	if(prctl(PR_SET_NAME, "itc_rx_sysvshm", 0, 0, 0) == -1)
	{
		// ERROR trace is needed here
		TPT_TRACE(TRACE_ERROR, "Failed to prctl()!");
		return NULL;
	}

	sprintf(itc_mbox_name, "itc_rx_sysvshm_0x%08x", sysvshm_inst.my_mbox_id_in_itccoord);

	sysvshm_inst.my_mbox_id = itc_create_mailbox(itc_mbox_name, ITC_NO_NAMESPACE);

	TPT_TRACE(TRACE_INFO, "Starting sysvshm_rx_thread %s...!", itc_mbox_name);
	int ret = pthread_setspecific(sysvshm_inst.destruct_key, (void*)(unsigned long)sysvshm_inst.my_mbox_id);
	if(ret != 0)
	{
		// ERROR trace is needed here
		TPT_TRACE(TRACE_ERROR, "pthread_setspecific error code = %d", ret);
		return NULL;
	}

	// Make this rx_sysvshm_thread cancellable without any cancellation point
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	struct sembuf asem[1];
	asem[0].sem_num = 0;
	asem[0].sem_flg = 0;

	MUTEX_UNLOCK(&sysvshm_inst.thread_mtx);
	for(;;)
	{
		if(sysvshm_inst.is_terminated)
		{
			TPT_TRACE(TRACE_INFO, "Terminating sysvshm rx thread!");
			break;
		}

		asem[0].sem_op = -1;
		if(semop(sysvshm_inst.my_sem_notify, asem, 1) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(my_sem_notify), errno = %d!", errno);
			break;
		}

		// Some processes has notified us about a new message
		// Accquire mutex lock to access the shared memory resources
		asem[0].sem_op = -1;
		if(semop(sysvshm_inst.my_sem_mutex, asem, 1) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(my_sem_mutex), errno = %d!", errno);
			break;
		}

		if(sysvshm_inst.my_shm_ptr->head.whichpool != -1 && sysvshm_inst.my_shm_ptr->head.whichslot != -1)
		{
			struct sysvshm_slot_info_t *slot;
			uint16_t num_unlimit_pages = 0;
			if(sysvshm_inst.my_shm_ptr->head.whichpool == POOL_UNLIMIT && sysvshm_inst.my_shm_ptr->num_unlimit_pages > 0)
			{
				num_unlimit_pages = sysvshm_inst.my_shm_ptr->num_unlimit_pages;
				if(sysvshm_inst.my_unlimit_shmid == 0 && sysvshm_inst.my_unlimit_shm_ptr == NULL)
				{
					get_my_unlimit_shm(num_unlimit_pages);
				}
			}

			unsigned long pool_offset = sysvshm_inst.pool_offset[sysvshm_inst.my_shm_ptr->head.whichpool];
			unsigned long slot_offset = sysvshm_inst.slot_sizes[sysvshm_inst.my_shm_ptr->head.whichpool]*sysvshm_inst.my_shm_ptr->head.whichslot;
			slot = (struct sysvshm_slot_info_t *)((unsigned long)sysvshm_inst.my_shm_ptr + pool_offset + slot_offset);

			if(slot->is_in_use == false)
			{
				TPT_TRACE(TRACE_ERROR, "Slot is not in use, pool type = %u!", slot->pool_type);
			} else if(slot->pool_type > POOL_UNLIMIT)
			{
				TPT_TRACE(TRACE_ERROR, "Invalid pool type = %u!", slot->pool_type);
			} else
			{
				if(sysvshm_inst.my_shm_ptr->head.whichpool == POOL_UNLIMIT && sysvshm_inst.my_shm_ptr->num_unlimit_pages > 0)
				{
					handle_received_message((struct itc_message *)sysvshm_inst.my_unlimit_shm_ptr);
				} else
				{
					handle_received_message((struct itc_message *)((unsigned long)slot + SYSVSHM_SLOT_HEADER_SIZE));
				}
			}

			// Remove slot from queue
			if(sysvshm_inst.my_shm_ptr->head.whichpool == sysvshm_inst.my_shm_ptr->tail.whichpool && \
			sysvshm_inst.my_shm_ptr->head.whichslot == sysvshm_inst.my_shm_ptr->tail.whichslot)
			{
				// TPT_TRACE(TRACE_INFO, "SYSV shared memory queue has only one slot, dequeue it!"); // TBD
				// Both head and tail should be moved to NULL
				sysvshm_inst.my_shm_ptr->head.whichpool = -1;
				sysvshm_inst.my_shm_ptr->head.whichslot = -1;
				sysvshm_inst.my_shm_ptr->tail.whichpool = -1;
				sysvshm_inst.my_shm_ptr->tail.whichslot = -1;
			} else
			{
				// In case queue has more than one items, move head to the 2nd item and remove the 1st item via prev
				// pointer of the 2nd.
				sysvshm_inst.my_shm_ptr->head.whichpool = slot->next_slot.whichpool;
				sysvshm_inst.my_shm_ptr->head.whichslot = slot->next_slot.whichslot;
			}

			// Clean up slot's header info
			slot->is_in_use = false;
			slot->next_slot.whichpool = -1;
			slot->next_slot.whichslot = -1;

			/* FIXME: Still don't know why but comment out this if-block will lower send-receive latency from 7.5ms to 3.5ms */
			// Unmap unlimited slot for a large itc message to save memory
			if(num_unlimit_pages > 0)
			{
				if (shmdt(sysvshm_inst.my_unlimit_shm_ptr) == -1) {
					TPT_TRACE(TRACE_ERROR, "Failed to shmdt, errno = %d!", errno);
				}

				if (shmctl(sysvshm_inst.my_unlimit_shmid, IPC_RMID, NULL) == -1)
				{
					TPT_TRACE(TRACE_ERROR, "Failed to IPC_RMID shmctl(my_unlimit_shmid), errno = %d", errno);
				}

				sysvshm_inst.my_unlimit_shmid = 0;
				sysvshm_inst.my_unlimit_shm_ptr = NULL;
				sysvshm_inst.my_shm_ptr->num_unlimit_pages = 0;
				sysvshm_inst.my_shm_ptr->num_pages = SYSVSHM_STATIC_ALLOC_PAGES;
			}
		} else
		{
			TPT_TRACE(TRACE_ERROR, "Some process has notified of new incoming message, but no message found in shared memory!");
		}

		// Release mutex lock
		asem[0].sem_op = 1;
		if(semop(sysvshm_inst.my_sem_mutex, asem, 1) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_post(my_sem_mutex), errno = %d!", errno);
			break;
		}

		// Allow new process take a slot in queue
		asem[0].sem_op = 1;
		if(semop(sysvshm_inst.my_sem_slot, asem, 1) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_post(my_sem_slot), errno = %d!", errno);
			break;
		}
	}

	return NULL;
}


/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void init_sysvshm(struct result_code* rc)
{
	int proj_id;
	key_t shm_key;

	proj_id = (sysvshm_inst.my_mbox_id_in_itccoord >> sysvshm_inst.itccoord_shift);
	shm_key = ftok(ITC_SYSVSHM_FILENAME, proj_id);
	if(shm_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.my_shmid = shmget(shm_key, SYSVSHM_STATIC_ALLOC_PAGES * SYSVSHM_PAGE_SIZE, 0666 | IPC_CREAT | IPC_EXCL);
	if(sysvshm_inst.my_shmid == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.my_shm_ptr = shmat(sysvshm_inst.my_shmid, NULL, 0);
	if(sysvshm_inst.my_shm_ptr == (struct sysvshm_metadata_t *)-1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmat, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// Locked, till we finish initialization
	sysvshm_inst.sem_attr.val = 0;
	if (semctl(sysvshm_inst.my_sem_mutex, 0, SETVAL, sysvshm_inst.sem_attr) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to SETVAL semctl(my_sem_mutex), errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	init_sysvshm_mempool(sysvshm_inst.my_shm_ptr);

	// Done initialization, allow other processes to access our shared memory now
	sysvshm_inst.sem_attr.val = 1;
	if (semctl(sysvshm_inst.my_sem_mutex, 0, SETVAL, sysvshm_inst.sem_attr) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to SETVAL semctl(my_sem_mutex), errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}

static void init_sysvshm_semaphores(struct result_code* rc)
{
	int proj_id;
	key_t sem_key;

	proj_id = (sysvshm_inst.my_mbox_id_in_itccoord >> sysvshm_inst.itccoord_shift);

	// ITC_SYSVSHM_SEM_MUTEX
	sem_key = ftok(ITC_SYSVSHM_SEM_MUTEX, proj_id);
	if(sem_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.my_sem_mutex = semget(sem_key, 1, 0666 | IPC_CREAT | IPC_EXCL);
	if(sysvshm_inst.my_sem_mutex == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// ITC_SYSVSHM_SEM_NOTIFY
	sem_key = ftok(ITC_SYSVSHM_SEM_NOTIFY, proj_id);
	if(sem_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.my_sem_notify = semget(sem_key, 1, 0666 | IPC_CREAT | IPC_EXCL);
	if(sysvshm_inst.my_sem_notify == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.sem_attr.val = 0;
	if (semctl(sysvshm_inst.my_sem_notify, 0, SETVAL, sysvshm_inst.sem_attr) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to SETVAL semctl(my_sem_notify), errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// ITC_SYSVSHM_SEM_SLOT
	sem_key = ftok(ITC_SYSVSHM_SEM_SLOT, proj_id);
	if(sem_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.my_sem_slot = semget(sem_key, 1, 0666 | IPC_CREAT | IPC_EXCL);
	if(sysvshm_inst.my_sem_slot == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.sem_attr.val = MAX_SYSVSHM_SLOTS;
	if (semctl(sysvshm_inst.my_sem_slot, 0, SETVAL, sysvshm_inst.sem_attr) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to SETVAL semctl(my_sem_slot), errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}

static void remove_sysvshm()
{
	if (shmdt((void *)sysvshm_inst.my_shm_ptr) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to shmdt, errno = %d!", errno);
	}

	if (sysvshm_inst.my_shmid != -1 && shmctl(sysvshm_inst.my_shmid, IPC_RMID, NULL) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to IPC_RMID shmctl(my_shmid), my_shmid = %d, errno = %d", sysvshm_inst.my_shmid, errno);
	}
}

static void remove_sysvshm_semaphores()
{
	if (semctl(sysvshm_inst.my_sem_mutex, 0, IPC_RMID, 0) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to IPC_RMID semctl(my_sem_mutex), errno = %d", errno);
		return;
	}

	if (semctl(sysvshm_inst.my_sem_notify, 0, IPC_RMID, 0) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to IPC_RMID semctl(my_sem_notify), errno = %d", errno);
		return;
	}

	if (semctl(sysvshm_inst.my_sem_slot, 0, IPC_RMID, 0) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to IPC_RMID semctl(my_sem_slot), errno = %d", errno);
		return;
	}
}

static void init_sysvshm_rx_thread(struct result_code* rc)
{
	int res = pthread_key_create(&sysvshm_inst.destruct_key, rxthread_destructor);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_key_create, error code = %d", res);
		remove_sysvshm();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = pthread_mutex_init(&sysvshm_inst.thread_mtx, NULL);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_mutex_init, error code = %d", res);
		remove_sysvshm();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	add_itcthread(rc, sysvshm_rx_thread, NULL, false, &sysvshm_inst.thread_mtx);
}

static void init_sysvshm_mempool(struct sysvshm_metadata_t *shm_ptr)
{
	struct sysvshm_metadata_t *metadata = shm_ptr;

	// At this moment owner don't need to lock my_sem_mutex since it's = 0 currently after initialization
	metadata->head.whichpool = -1;
	metadata->head.whichslot = -1;
	metadata->tail.whichpool = -1;
	metadata->tail.whichslot = -1;
	metadata->num_pages = SYSVSHM_STATIC_ALLOC_PAGES;
	metadata->num_unlimit_pages = 0;

	struct sysvshm_slot_info_t *slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_96]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_96]; ++i)
	{
		slot_header->pool_type = POOL_96;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 128);
	}

	slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_480]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_480]; ++i)
	{
		slot_header->pool_type = POOL_480;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 512);
	}

	slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_992]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_992]; ++i)
	{
		slot_header->pool_type = POOL_992;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 1024);
	}

	slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_2016]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_2016]; ++i)
	{
		slot_header->pool_type = POOL_2016;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 2048);
	}

	slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_4064]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_4064]; ++i)
	{
		slot_header->pool_type = POOL_4064;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 4096);
	}

	slot_header = (struct sysvshm_slot_info_t *)((unsigned long)metadata + sysvshm_inst.pool_offset[POOL_16352]);
	for(unsigned char i = 0; i < sysvshm_inst.num_slots[POOL_16352]; ++i)
	{
		slot_header->pool_type = POOL_16352;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct sysvshm_slot_info_t *)((unsigned long)slot_header + 16384);
	}
}

static void release_sysvshm_resources(struct result_code* rc)
{
	remove_sysvshm();
	remove_sysvshm_semaphores();
	release_sysvshm_contactlist();

	int ret = pthread_key_delete(sysvshm_inst.destruct_key);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_key_delete, error code = %d", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	ret = pthread_mutex_destroy(&sysvshm_inst.thread_mtx);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "pthread_mutex_destroy error code = %d", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sysvshm_inst.is_terminated = 1;
	sysvshm_inst.is_initialized = -1;
	memset(&sysvshm_inst, 0, sizeof(struct sysvshm_instance));
}

static void init_unilimit_sysvshm(struct result_code* rc, uint16_t num_unlimit_pages, struct sysvshm_contactlist* cl)
{
	int proj_id;
	key_t shm_key;

	proj_id = ((cl->mbox_id_in_itccoord & sysvshm_inst.itccoord_mask) >> sysvshm_inst.itccoord_shift);

	shm_key = ftok(ITC_SYSVSHM_UNLIMIT_FILENAME, proj_id);
	if(shm_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	cl->unlimit_sysvshm_id = shmget(shm_key, (SYSVSHM_STATIC_ALLOC_PAGES + num_unlimit_pages) * SYSVSHM_PAGE_SIZE, 0666 | IPC_CREAT);
	if(cl->unlimit_sysvshm_id == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	cl->unlimit_shm_ptr = shmat(cl->unlimit_sysvshm_id, NULL, 0);
	if(cl->unlimit_shm_ptr == (struct sysvshm_metadata_t *)-1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmat, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}

static void get_my_unlimit_shm(uint16_t num_unlimit_pages)
{
	int proj_id;
	key_t shm_key;

	proj_id = ((sysvshm_inst.my_mbox_id_in_itccoord & sysvshm_inst.itccoord_mask) >> sysvshm_inst.itccoord_shift);

	shm_key = ftok(ITC_SYSVSHM_UNLIMIT_FILENAME, proj_id);
	if(shm_key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		return;
	}

	sysvshm_inst.my_unlimit_shmid = shmget(shm_key, (SYSVSHM_STATIC_ALLOC_PAGES + num_unlimit_pages) * SYSVSHM_PAGE_SIZE, 0);
	if(sysvshm_inst.my_unlimit_shmid == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmget, errno = %d", errno);
		return;
	}

	sysvshm_inst.my_unlimit_shm_ptr = shmat(sysvshm_inst.my_unlimit_shmid, NULL, 0);
	if(sysvshm_inst.my_unlimit_shm_ptr == (struct sysvshm_metadata_t *)-1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmat, errno = %d", errno);
		return;
	}
}

static struct sysvshm_contactlist* get_sysvshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	struct sysvshm_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	if(cl == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Contact list not found!");
		return NULL;
	}

	if(cl->mbox_id_in_itccoord == 0)
	{
		// TPT_TRACE(TRACE_INFO, "Add contact list!"); // TBD
		add_sysvshm_cl(rc, cl, mbox_id);
	}

	return cl;
}

static struct sysvshm_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t pid;

	pid = (mbox_id & sysvshm_inst.itccoord_mask) >> sysvshm_inst.itccoord_shift;
	if(pid == 0 || pid >= MAX_SUPPORTED_PROCESSES)
	{
		TPT_TRACE(TRACE_ABN, "Invalid contact list's process id!");
		rc->flags |= ITC_INVALID_ARGUMENTS;
		return NULL;
	}

	return &(sysvshm_inst.sysvshm_cl[pid]);
}

static void add_sysvshm_cl(struct result_code* rc, struct sysvshm_contactlist* cl, itc_mbox_id_t mbox_id)
{
	int shmid;
	struct sysvshm_metadata_t *shm_ptr;
	int sem_mutex;
	int sem_notify;
	int sem_slot;
	int proj_id;
	key_t key;
	itc_mbox_id_t new_mbx_id;


	new_mbx_id = mbox_id & sysvshm_inst.itccoord_mask;
	proj_id = (new_mbx_id >> sysvshm_inst.itccoord_shift);

	// ITC_SYSVSHM_FILENAME
	key = ftok(ITC_SYSVSHM_FILENAME, proj_id);
	if(key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	shmid = shmget(key, SYSVSHM_STATIC_ALLOC_PAGES * SYSVSHM_PAGE_SIZE, 0);
	if(shmid == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	shm_ptr = shmat(shmid, NULL, 0);
	if(shm_ptr == (struct sysvshm_metadata_t *)-1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shmat, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// ITC_SYSVSHM_SEM_MUTEX
	key = ftok(ITC_SYSVSHM_SEM_MUTEX, proj_id);
	if(key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sem_mutex = semget(key, 1, 0);
	if(sem_mutex == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// ITC_SYSVSHM_SEM_NOTIFY
	key = ftok(ITC_SYSVSHM_SEM_NOTIFY, proj_id);
	if(key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sem_notify = semget(key, 1, 0);
	if(sem_notify == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// ITC_SYSVSHM_SEM_SLOT
	key = ftok(ITC_SYSVSHM_SEM_SLOT, proj_id);
	if(key == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftok, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sem_slot = semget(key, 1, 0);
	if(sem_slot == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to semget, errno = %d", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	cl->mbox_id_in_itccoord = (mbox_id & sysvshm_inst.itccoord_mask);
	cl->sysvshm_id = shmid;
	cl->metadata = shm_ptr;
	cl->m_sem_mutex = sem_mutex;
	cl->m_sem_notify = sem_notify;
	cl->m_sem_slot = sem_slot;
	cl->unlimit_sysvshm_id = -1;
	cl->unlimit_shm_ptr = NULL;
}

static void remove_sysvshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	(void)rc;
	struct sysvshm_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	cl->mbox_id_in_itccoord = 0;
	cl->sysvshm_id = 0;
	cl->metadata = NULL;
	cl->m_sem_mutex = 0;
	cl->m_sem_notify = 0;
	cl->m_sem_slot = 0;
	cl->unlimit_sysvshm_id = 0;
	cl->unlimit_shm_ptr = NULL;
}

static void handle_received_message(struct itc_message *p_message)
{
	struct itc_message* message;
	union itc_msg* msg;
	uint16_t flags;

	char *endpoint = (char*)((unsigned long)(&p_message->msgno) + p_message->size);
	if(*endpoint != ENDPOINT)
	{
		TPT_TRACE(TRACE_ABN, "Received malform message from some mailbox, invalid ENDPOINT 0x%02x!", *endpoint & 0xFF);
		return;
	}

#ifdef UNITTEST
	struct itc_message* tmp_message;
	tmp_message = (struct itc_message *)malloc(p_message->size + ITC_HEADER_SIZE + 1);
	tmp_message->flags = 0;
	msg = CONVERT_TO_MSG(tmp_message);
#else
	msg = itc_alloc(p_message->size, 0);
#endif

	message = CONVERT_TO_MESSAGE(msg);

	flags = message->flags; // Saved flags
	memcpy(message, p_message, (p_message->size + ITC_HEADER_SIZE + 1));
	message->flags = flags; // Retored flags

#ifdef UNITTEST
	// Simulate that everything is ok at this point. Do nothing in unit test.
	// API itc_send is an external interface, so do not care about it if everything we pass into it is all correct.
	TPT_TRACE(TRACE_DEBUG, "ENTER UNITTEST!");
	free(tmp_message);
#else

	// TPT_TRACE(TRACE_INFO, "Forwarding a message to local mailbox from external mbox = 0x%08x", message->sender); // TBD
	itc_send(&msg, message->receiver, ITC_MY_MBOX_ID, NULL);
#endif
}

static void rxthread_destructor(void* data)
{
	(void)data;

	if(sysvshm_inst.my_shmid != -1)
	{
		sysvshm_inst.is_terminated = 1;
		if(shmctl(sysvshm_inst.my_shmid, IPC_RMID, NULL) == -1)
		{
			// ERROR trace is needed here
			TPT_TRACE(TRACE_ERROR, "Failed to shmctl");
		}
		sysvshm_inst.my_shmid = -1;
	}
}

static void generate_msqfile(struct result_code* rc)
{
	FILE* fd;
	int res;

	res = mkdir(ITC_BASE_PATH, 0777);

	if(res < 0 && errno != EEXIST)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to mkdir /tmp/itc/");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = mkdir(ITC_SYSVSHM_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to mkdir /tmp/itc/sysvshm/");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_FOLDER, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this folder earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	fd = fopen(ITC_SYSVSHM_FILENAME, "w");
	if(fd == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_FILENAME, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this file earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	TPT_TRACE(TRACE_INFO, "Open file %s successfully!", ITC_SYSVSHM_FILENAME);

	if(fclose(fd) != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	fd = fopen(ITC_SYSVSHM_UNLIMIT_FILENAME, "w");
	if(fd == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_UNLIMIT_FILENAME, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this file earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	TPT_TRACE(TRACE_INFO, "Open file %s successfully!", ITC_SYSVSHM_UNLIMIT_FILENAME);

	if(fclose(fd) != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
	
	fd = fopen(ITC_SYSVSHM_SEM_MUTEX, "w");
	if(fd == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_SEM_MUTEX, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this file earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	TPT_TRACE(TRACE_INFO, "Open file %s successfully!", ITC_SYSVSHM_SEM_MUTEX);

	if(fclose(fd) != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
	
	fd = fopen(ITC_SYSVSHM_SEM_NOTIFY, "w");
	if(fd == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_SEM_NOTIFY, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this file earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	TPT_TRACE(TRACE_INFO, "Open file %s successfully!", ITC_SYSVSHM_SEM_NOTIFY);

	if(fclose(fd) != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
	
	fd = fopen(ITC_SYSVSHM_SEM_SLOT, "w");
	if(fd == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fopen");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = chmod(ITC_SYSVSHM_SEM_SLOT, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod");
		// Will not "return" over here, just ignore it because maybe other users created this file earlier
		// rc->flags |= ITC_SYSCALL_ERROR;
		// return;
	}

	TPT_TRACE(TRACE_INFO, "Open file %s successfully!", ITC_SYSVSHM_SEM_SLOT);

	if(fclose(fd) != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fclose");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}

static void release_sysvshm_contactlist()
{
	for(int i = 0; i < MAX_SUPPORTED_PROCESSES; ++i)
	{
		if(sysvshm_inst.sysvshm_cl[i].mbox_id_in_itccoord != 0)
		{
			if (shmdt((void *)sysvshm_inst.sysvshm_cl[i].metadata) == -1) {
				TPT_TRACE(TRACE_ERROR, "Failed to shmdt, errno = %d!", errno);
			}

			sysvshm_inst.sysvshm_cl[i].mbox_id_in_itccoord = 0;
			sysvshm_inst.sysvshm_cl[i].sysvshm_id = 0;
			sysvshm_inst.sysvshm_cl[i].metadata = NULL;
			sysvshm_inst.sysvshm_cl[i].m_sem_mutex = 0;
			sysvshm_inst.sysvshm_cl[i].m_sem_notify = 0;
			sysvshm_inst.sysvshm_cl[i].m_sem_slot = 0;
			sysvshm_inst.sysvshm_cl[i].unlimit_shm_ptr = NULL;
			sysvshm_inst.sysvshm_cl[i].unlimit_sysvshm_id = 0;
		}
	}
}







