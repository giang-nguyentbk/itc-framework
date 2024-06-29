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

#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"
#include "itc_threadmanager.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                    INTERNAL TYPES IN POSIXSHM-ATOR                   *****
*******************************************************************************/
#define POSIXSHM_PAGE_SIZE		4096
#define POSIXSHM_SLOT_HEADER_SIZE	32
#define MAX_POSIXSHM_SLOTS		70
#define POSIXSHM_STATIC_ALLOC_PAGES	25
#define NUM_SLOTS_POOL_480		16
#define NUM_SLOTS_POOL_992		8
#define NUM_SLOTS_POOL_2016		8
#define NUM_SLOTS_POOL_4064		4
#define NUM_SLOTS_POOL_16352		3
#define NUM_SLOTS_POOL_UNLIMIT		1

enum posixshm_pool_type_e {
	POOL_96 = 0, // 1 page (same page as metadata) - ((POSIXSHM_PAGE_SIZE - (sizeof(posixshm_metadata_t)/128 + 1)*128) / 128) slots (< 32 slots)
	POOL_480, // 2 pages, 16 slots
	POOL_992, // 2 pages, 8 slots
	POOL_2016, // 4 pages, 8 slots
	POOL_4064, // 4 pages, 4 slots
	POOL_16352, // 12 pages, 3 slots -> statically allocated pages in total = 25 pages ~ 100KB
	POOL_UNLIMIT, // n pages (size > 16352 bytes), only 1 slot at a time -> dynamically allocated pages
	NUM_POOLS
};

struct posixshm_pool_slot {
	int					whichpool;
	int					whichslot;
};

struct posixshm_slot_info_t {
	struct posixshm_pool_slot		next_slot;
	unsigned char				pool_type;
	bool					is_in_use;
};

struct posixshm_metadata_t {
	uint16_t				num_pages;
	uint16_t				num_unlimit_pages;
	struct posixshm_pool_slot		head; // to let receiver dequeue messages
	struct posixshm_pool_slot		tail; // to let sender enqueue messages
};

struct posixshm_contactlist {
	itc_mbox_id_t				mbox_id_in_itccoord;
	int					posixshm_id;
	struct posixshm_metadata_t		*metadata;
	// Binary semaphore indicating that only one process can access shared memory at a time
	sem_t					*m_sem_mutex;
	// Binary semaphore used by sender to trigger a notification towards receiver telling them that a new message has been enqueued
	// So receiver will block on sem_wait(m_sem_notify) while sender will call sem_post(m_sem_notify)
	sem_t					*m_sem_notify;
	// Counting semaphore used by receiver to indicate that a message has been dequeued and queue buffer has new slot for another message
	sem_t					*m_sem_slot;
};

struct posixshm_instance {
	itc_mbox_id_t           	my_mbox_id_in_itccoord;
	itc_mbox_id_t           	itccoord_mask;
        itc_mbox_id_t           	itccoord_shift;

	int				my_shmid;
	char				my_posixshm_name[64];
	struct posixshm_metadata_t	*my_shm_ptr;

	sem_t				*my_sem_mutex;
	char				my_sem_mutex_name[64];
	sem_t				*my_sem_notify;
	char				my_sem_notify_name[64];
	sem_t				*my_sem_slot;
	char				my_sem_slot_name[64];

	int				is_initialized;
	int				is_terminated;

	itc_mbox_id_t			my_mbox_id;
	pthread_mutex_t			thread_mtx;
	pthread_key_t			destruct_key;

	unsigned char			num_slots[NUM_POOLS]; // how many slots in each pool
	int16_t				slot_sizes[NUM_POOLS]; // get slot's size of a specific pool, for example 128, 512,... and -1 for unlimited pool
	unsigned long			pool_offset[NUM_POOLS]; // get slot's size of a specific pool, for example 128, 512,... and -1 for unlimited pool

	struct posixshm_contactlist	posixshm_cl[MAX_SUPPORTED_PROCESSES];
};


/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct posixshm_instance posixshm_inst; // One instance per a process, multiple threads all use this one.



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void init_posixshm(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord);
static void init_posixshm_semaphores(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord);
static void remove_posixshm();
static void remove_posixshm_semaphores();
static void init_posixshm_rx_thread(struct result_code* rc);
static void init_posixshm_mempool(struct posixshm_metadata_t *shm_ptr);
static void release_posixshm_resources(struct result_code* rc);
static struct posixshm_contactlist* get_posixshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static struct posixshm_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id);
static void add_posixshm_cl(struct result_code* rc, struct posixshm_contactlist* cl, itc_mbox_id_t mbox_id);
// static void remove_posixshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id);

static void handle_received_message(struct posixshm_slot_info_t *slot);
static void rxthread_destructor(void* data);




/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void posixshm_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags);

static void posixshm_exit(struct result_code* rc);

static void posixshm_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static void* posixshm_rx_thread(void *data);

struct itci_transport_apis posixshm_trans_apis = { NULL,
                                            	posixshm_init,
                                            	posixshm_exit,
                                            	NULL,
                                            	NULL,
                                            	posixshm_send,
                                            	NULL,
                                            	NULL,
                                            	NULL };





/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
void posixshm_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;

	if(posixshm_inst.is_initialized == 1)
	{
		if(flags & ITC_FLAGS_FORCE_REINIT)
		{
			TPT_TRACE(TRACE_INFO, "Force re-initializing!");
			release_posixshm_resources(rc);
			if(rc != ITC_OK)
			{
				TPT_TRACE(TRACE_ERROR, "Failed to release_posixshm_resources!");
				return;
			}
		} else
		{
			TPT_TRACE(TRACE_INFO, "Already initialized!");
			rc->flags |= ITC_ALREADY_INIT;
			return;
		}
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

	posixshm_inst.itccoord_mask 		= itccoord_mask;
	posixshm_inst.itccoord_shift		= tmp_shift;
	posixshm_inst.my_mbox_id_in_itccoord 	= my_mbox_id_in_itccoord;

	posixshm_inst.num_slots[POOL_96]	= (POSIXSHM_PAGE_SIZE / 128) - (sizeof(struct posixshm_metadata_t)/128 + 1);
	posixshm_inst.num_slots[POOL_480]	= NUM_SLOTS_POOL_480;
	posixshm_inst.num_slots[POOL_992]	= NUM_SLOTS_POOL_992;
	posixshm_inst.num_slots[POOL_2016]	= NUM_SLOTS_POOL_2016;
	posixshm_inst.num_slots[POOL_4064]	= NUM_SLOTS_POOL_4064;
	posixshm_inst.num_slots[POOL_16352]	= NUM_SLOTS_POOL_16352;
	posixshm_inst.num_slots[POOL_UNLIMIT]	= NUM_SLOTS_POOL_UNLIMIT;

	posixshm_inst.slot_sizes[POOL_96]	= 128;
	posixshm_inst.slot_sizes[POOL_480]	= 512;
	posixshm_inst.slot_sizes[POOL_992]	= 1024;
	posixshm_inst.slot_sizes[POOL_2016]	= 2048;
	posixshm_inst.slot_sizes[POOL_4064]	= 4096;
	posixshm_inst.slot_sizes[POOL_16352]	= 16384;
	posixshm_inst.slot_sizes[POOL_UNLIMIT]	= -1;

	posixshm_inst.pool_offset[POOL_96]		= (sizeof(struct posixshm_metadata_t)/128 + 1)*128;
	posixshm_inst.pool_offset[POOL_480]		= POSIXSHM_PAGE_SIZE;
	posixshm_inst.pool_offset[POOL_992]		= 3 * POSIXSHM_PAGE_SIZE;
	posixshm_inst.pool_offset[POOL_2016]		= 5 * POSIXSHM_PAGE_SIZE;
	posixshm_inst.pool_offset[POOL_4064]		= 9 * POSIXSHM_PAGE_SIZE;
	posixshm_inst.pool_offset[POOL_16352]		= 13 * POSIXSHM_PAGE_SIZE;
	posixshm_inst.pool_offset[POOL_UNLIMIT]		= 25 * POSIXSHM_PAGE_SIZE;

	init_posixshm_semaphores(rc, my_mbox_id_in_itccoord);

	init_posixshm(rc, my_mbox_id_in_itccoord);
	
	init_posixshm_rx_thread(rc);

	posixshm_inst.is_initialized = 1;
}

static void posixshm_exit(struct result_code* rc)
{
	if(!posixshm_inst.is_initialized)
	{
		TPT_TRACE(TRACE_ABN, "Not initialized yet!");
		rc->flags |= ITC_NOT_INIT_YET;
		return;
	}

	release_posixshm_resources(rc);
}

static void posixshm_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to)
{
	struct posixshm_contactlist* cl;
	int res = -1;

	if(!posixshm_inst.is_initialized)
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
		if(posixshm_inst.slot_sizes[whichpool] > 0 && (message->size + ITC_HEADER_SIZE + 1 + 32) <= (uint32_t)posixshm_inst.slot_sizes[whichpool])
		{
			break;
		}
	}

	if(whichpool == NUM_POOLS)
	{
		whichpool = POOL_UNLIMIT;
		num_unlimit_pages = (message->size + ITC_HEADER_SIZE + 1 + 32) / POSIXSHM_PAGE_SIZE + 1;
	}

	cl = get_posixshm_cl(rc, to);
	if(cl == NULL || cl->mbox_id_in_itccoord == 0)
	{
		TPT_TRACE(TRACE_ABN, "Receiver side not initialised POSIX shared memory yet!");
		rc->flags &= ~ITC_SYSCALL_ERROR; // The receiver side has not initialised message queue yet
		rc->flags |= ITC_QUEUE_NULL; // So remove unecessary syscall error, return an ITC_QUEUE_NULL warning instead. This is not an ERROR at all!
		/* If send failed, users have to self-free the message. ITC system only free messages when send successfully */
		return;
	}

	// Get into a line before allowed inserting an itc message into receiver shared memory's queue
	// Have to wait if receiver queue has been fulled with MAX_POSIXSHM_SLOTS messages
	if(sem_wait(cl->m_sem_slot) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(cl->m_sem_slot), mbox_id_in_itccoord = 0x%08x, errno = %d!", cl->mbox_id_in_itccoord, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// Accquire mutex lock to access receiver's shared memory resources
	if(sem_wait(cl->m_sem_mutex) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(cl->m_sem_mutex), mbox_id_in_itccoord = 0x%08x, errno = %d!", cl->mbox_id_in_itccoord, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// Critical section
	struct posixshm_slot_info_t *new_slot;
	unsigned char new_slot_index = 0;
	if(whichpool == POOL_UNLIMIT && num_unlimit_pages > (posixshm_inst.slot_sizes[POOL_16352] / POSIXSHM_PAGE_SIZE))
	{
		if(munmap(cl->metadata, POSIXSHM_STATIC_ALLOC_PAGES * POSIXSHM_PAGE_SIZE) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to munmap(POSIXSHM_STATIC_ALLOC_PAGES), errno = %d!", errno);
			return;
		}

		res = ftruncate(cl->posixshm_id, (POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages) * POSIXSHM_PAGE_SIZE); // Assume page size is 4KB
		if(res < 0)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to ftruncate, res = %d, errno = %d!", res, errno);
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		cl->metadata = (struct posixshm_metadata_t *)mmap(NULL, (POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages) * POSIXSHM_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, cl->posixshm_id, 0);
		cl->metadata->num_unlimit_pages = num_unlimit_pages;
		cl->metadata->num_pages = POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages;
		new_slot = (struct posixshm_slot_info_t *)((unsigned long)cl->metadata + 25*POSIXSHM_PAGE_SIZE);
		new_slot->next_slot.whichpool = -1;
		new_slot->next_slot.whichslot = -1;
		new_slot->pool_type = POOL_UNLIMIT;
	} else
	{
		// Shopping for a slot in whichpool
		struct posixshm_slot_info_t *slot = (struct posixshm_slot_info_t *)((unsigned long)cl->metadata + posixshm_inst.pool_offset[whichpool]);
		for(; new_slot_index < posixshm_inst.num_slots[whichpool]; ++new_slot_index)
		{
			if(slot->is_in_use == false && slot->next_slot.whichpool == -1 && slot->next_slot.whichslot == -1)
			{
				new_slot = slot;
				break;
			}

			// Move to next slot in whichpool
			slot = (struct posixshm_slot_info_t *)((unsigned long)slot + posixshm_inst.slot_sizes[whichpool]);
		}

		if(new_slot_index == posixshm_inst.num_slots[whichpool])
		{
			TPT_TRACE(TRACE_ERROR, "No more slot available for this pool = %d, limit at %u slots!", whichpool, posixshm_inst.num_slots[whichpool]);
			return;
		}
	}

	if(cl->metadata->tail.whichpool == -1 && cl->metadata->tail.whichslot == -1)
	{
		// TPT_TRACE(TRACE_INFO, "POSIX shared memory queue is empty, add a new one!"); // TBD
		cl->metadata->head.whichpool = whichpool;
		cl->metadata->head.whichslot = new_slot_index;
		cl->metadata->tail.whichpool = whichpool;
		cl->metadata->tail.whichslot = new_slot_index;
	} else
	{
		unsigned long pool_offset = posixshm_inst.pool_offset[cl->metadata->tail.whichpool];
		unsigned long slot_offset = posixshm_inst.slot_sizes[cl->metadata->tail.whichpool]*cl->metadata->tail.whichslot;
		struct posixshm_slot_info_t *tail_node = (struct posixshm_slot_info_t *)((unsigned long)cl->metadata + pool_offset + slot_offset);
		tail_node->next_slot.whichpool = whichpool;
		tail_node->next_slot.whichslot = new_slot_index;
		cl->metadata->tail.whichpool = whichpool;
		cl->metadata->tail.whichslot = new_slot_index;
	}

	new_slot->is_in_use = true;

	memcpy((void *)((unsigned long)new_slot + POSIXSHM_SLOT_HEADER_SIZE), message, message->size + ITC_HEADER_SIZE + 1);

	// Release mutex lock
	if(sem_post(cl->m_sem_mutex) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_post(cl->m_sem_mutex), errno = %d!", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	// Notify receiver about incoming message
	if(sem_post(cl->m_sem_notify) == -1)
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

static void* posixshm_rx_thread(void *data)
{
	(void)data;

	char itc_mbox_name[30];

	if(prctl(PR_SET_NAME, "itc_rx_posixshm", 0, 0, 0) == -1)
	{
		// ERROR trace is needed here
		TPT_TRACE(TRACE_ERROR, "Failed to prctl()!");
		return NULL;
	}

	sprintf(itc_mbox_name, "itc_rx_posixshm_0x%08x", posixshm_inst.my_mbox_id_in_itccoord);

	posixshm_inst.my_mbox_id = itc_create_mailbox(itc_mbox_name, ITC_NO_NAMESPACE);

	TPT_TRACE(TRACE_INFO, "Starting posixshm_rx_thread %s...!", itc_mbox_name);
	int ret = pthread_setspecific(posixshm_inst.destruct_key, (void*)(unsigned long)posixshm_inst.my_mbox_id);
	if(ret != 0)
	{
		// ERROR trace is needed here
		TPT_TRACE(TRACE_ERROR, "pthread_setspecific error code = %d", ret);
		return NULL;
	}

	MUTEX_UNLOCK(&posixshm_inst.thread_mtx);
	for(;;)
	{
		if(posixshm_inst.is_terminated)
		{
			TPT_TRACE(TRACE_INFO, "Terminating posixshm rx thread!");
			break;
		}

		if(sem_wait(posixshm_inst.my_sem_notify) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(my_sem_notify), errno = %d!", errno);
			break;
		}

		// Some processes has notified us about a new message
		// Accquire mutex lock to access the shared memory resources
		if(sem_wait(posixshm_inst.my_sem_mutex) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_wait(my_sem_mutex), errno = %d!", errno);
			break;
		}

		if(posixshm_inst.my_shm_ptr->head.whichpool != -1 && posixshm_inst.my_shm_ptr->head.whichslot != -1)
		{
			struct posixshm_slot_info_t *slot;
			uint16_t num_unlimit_pages = 0;
			if(posixshm_inst.my_shm_ptr->head.whichpool == POOL_UNLIMIT && posixshm_inst.my_shm_ptr->num_unlimit_pages > 0)
			{
				num_unlimit_pages = posixshm_inst.my_shm_ptr->num_unlimit_pages;
				if(munmap(posixshm_inst.my_shm_ptr, POSIXSHM_STATIC_ALLOC_PAGES * POSIXSHM_PAGE_SIZE) == -1)
				{
					TPT_TRACE(TRACE_ERROR, "Failed to munmap(POSIXSHM_STATIC_ALLOC_PAGES), errno = %d!", errno);
					break;
				}

				posixshm_inst.my_shm_ptr = (struct posixshm_metadata_t *)mmap(NULL, (POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages)*POSIXSHM_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, posixshm_inst.my_shmid, 0);
			}

			unsigned long pool_offset = posixshm_inst.pool_offset[posixshm_inst.my_shm_ptr->head.whichpool];
			unsigned long slot_offset = posixshm_inst.slot_sizes[posixshm_inst.my_shm_ptr->head.whichpool]*posixshm_inst.my_shm_ptr->head.whichslot;
			slot = (struct posixshm_slot_info_t *)((unsigned long)posixshm_inst.my_shm_ptr + pool_offset + slot_offset);

			handle_received_message(slot);

			// Remove slot from queue
			if(posixshm_inst.my_shm_ptr->head.whichpool == posixshm_inst.my_shm_ptr->tail.whichpool && \
			posixshm_inst.my_shm_ptr->head.whichslot == posixshm_inst.my_shm_ptr->tail.whichslot)
			{
				// TPT_TRACE(TRACE_INFO, "POSIX shared memory queue has only one slot, dequeue it!"); // TBD
				// Both head and tail should be moved to NULL
				posixshm_inst.my_shm_ptr->head.whichpool = -1;
				posixshm_inst.my_shm_ptr->head.whichslot = -1;
				posixshm_inst.my_shm_ptr->tail.whichpool = -1;
				posixshm_inst.my_shm_ptr->tail.whichslot = -1;
			} else
			{
				// In case queue has more than one items, move head to the 2nd item and remove the 1st item via prev
				// pointer of the 2nd.
				posixshm_inst.my_shm_ptr->head.whichpool = slot->next_slot.whichpool;
				posixshm_inst.my_shm_ptr->head.whichslot = slot->next_slot.whichslot;
			}

			// Clean up slot's header info
			slot->is_in_use = false;
			slot->next_slot.whichpool = -1;
			slot->next_slot.whichslot = -1;

			// Unmap unlimited slot for a large itc message to save memory
			if(num_unlimit_pages > 0)
			{
				if(munmap(posixshm_inst.my_shm_ptr, (POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages)*POSIXSHM_PAGE_SIZE) == -1)
				{
					TPT_TRACE(TRACE_ERROR, "Failed to munmap(POSIXSHM_STATIC_ALLOC_PAGES + num_unlimit_pages), errno = %d!", errno);
					break;
				}

				posixshm_inst.my_shm_ptr = (struct posixshm_metadata_t *)mmap(NULL, POSIXSHM_STATIC_ALLOC_PAGES*POSIXSHM_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, posixshm_inst.my_shmid, 0);
				posixshm_inst.my_shm_ptr->num_unlimit_pages = 0;
				posixshm_inst.my_shm_ptr->num_pages = POSIXSHM_STATIC_ALLOC_PAGES;
			}
		} else
		{
			TPT_TRACE(TRACE_ERROR, "Some process has notified of new incoming message, but no message found in shared memory!");
		}

		// Release mutex lock
		if(sem_post(posixshm_inst.my_sem_mutex) == -1)
		{
			TPT_TRACE(TRACE_ERROR, "Failed to sem_post(my_sem_mutex), errno = %d!", errno);
			break;
		}

		// Allow new process take a slot in queue
		if(sem_post(posixshm_inst.my_sem_slot) == -1)
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
static void init_posixshm(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord)
{
	sprintf(posixshm_inst.my_posixshm_name, "/itc_posixshm_0x%08x", my_mbox_id_in_itccoord);
	if ((posixshm_inst.my_shmid = shm_open(posixshm_inst.my_posixshm_name, O_RDWR | O_CREAT | O_EXCL, 0666)) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to mq_open, errno = %d!", errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		remove_posixshm_semaphores();
		return;
	}

	int res = fchmod(posixshm_inst.my_shmid, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to fchmod, file = %s, res = %d, errno = %d!", posixshm_inst.my_posixshm_name, res, errno);
		remove_posixshm();
		remove_posixshm_semaphores();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = ftruncate(posixshm_inst.my_shmid, POSIXSHM_STATIC_ALLOC_PAGES * POSIXSHM_PAGE_SIZE); // Assume page size is 4KB
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to ftruncate, res = %d, errno = %d!", res, errno);
		remove_posixshm();
		remove_posixshm_semaphores();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	posixshm_inst.my_shm_ptr = (struct posixshm_metadata_t *)mmap(NULL, POSIXSHM_STATIC_ALLOC_PAGES * POSIXSHM_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, posixshm_inst.my_shmid, 0);
	init_posixshm_mempool(posixshm_inst.my_shm_ptr);

	// Done initialization, allow other processes to access our shared memory now
	sem_post(posixshm_inst.my_sem_mutex);
}

static void init_posixshm_semaphores(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord)
{
	sprintf(posixshm_inst.my_sem_mutex_name, "/itc_posixshm_sem_mutex_0x%08x", my_mbox_id_in_itccoord);
	if ((posixshm_inst.my_sem_mutex = sem_open(posixshm_inst.my_sem_mutex_name, O_CREAT | O_EXCL, 0666, 0)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", posixshm_inst.my_sem_mutex_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	char path[128];
	sprintf(path, "/dev/shm/sem.%s", posixshm_inst.my_sem_mutex_name + 1);
	int res = chmod(path, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod, file = %s, res = %d, errno = %d!", path, res, errno);
		remove_posixshm_semaphores();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
	
	sprintf(posixshm_inst.my_sem_notify_name, "/itc_posixshm_sem_notify_0x%08x", my_mbox_id_in_itccoord);
	if ((posixshm_inst.my_sem_notify = sem_open(posixshm_inst.my_sem_notify_name, O_CREAT | O_EXCL, 0666, 0)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", posixshm_inst.my_sem_notify_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		remove_posixshm_semaphores();
		return;
	}

	sprintf(path, "/dev/shm/sem.%s", posixshm_inst.my_sem_notify_name + 1);
	res = chmod(path, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod, file = %s, res = %d, errno = %d!", path, res, errno);
		remove_posixshm_semaphores();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sprintf(posixshm_inst.my_sem_slot_name, "/itc_posixshm_sem_slot_0x%08x", my_mbox_id_in_itccoord);
	if ((posixshm_inst.my_sem_slot = sem_open(posixshm_inst.my_sem_slot_name, O_CREAT | O_EXCL, 0666, MAX_POSIXSHM_SLOTS)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", posixshm_inst.my_sem_slot_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		remove_posixshm_semaphores();
		return;
	}

	sprintf(path, "/dev/shm/sem.%s", posixshm_inst.my_sem_slot_name + 1);
	res = chmod(path, 0777);
	if(res < 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to chmod, file = %s, res = %d, errno = %d!", path, res, errno);
		remove_posixshm_semaphores();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}

static void remove_posixshm()
{
	if (close(posixshm_inst.my_shmid) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to close, my_shmid = %d, errno = %d!", posixshm_inst.my_shmid, errno);
	}

	if (shm_unlink(posixshm_inst.my_posixshm_name) == -1) {
		TPT_TRACE(TRACE_ERROR, "Failed to mq_unlink, my_posixshm_name = %s, errno = %d!", posixshm_inst.my_posixshm_name, errno);
	}
}

static void remove_posixshm_semaphores()
{
	if(sem_close(posixshm_inst.my_sem_mutex) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_mutex, errno = %d!", errno);
	}

	if(sem_unlink(posixshm_inst.my_sem_mutex_name) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_mutex_name, errno = %d!", errno);
	}

	if(sem_close(posixshm_inst.my_sem_notify) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_notify, errno = %d!", errno);
	}

	if(sem_unlink(posixshm_inst.my_sem_notify_name) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_notify_name, errno = %d!", errno);
	}

	if(sem_close(posixshm_inst.my_sem_slot) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_slot, errno = %d!", errno);
	}

	if(sem_unlink(posixshm_inst.my_sem_slot_name) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to sem_close, my_sem_slot_name, errno = %d!", errno);
	}
	
}

static void init_posixshm_rx_thread(struct result_code* rc)
{
	int res = pthread_key_create(&posixshm_inst.destruct_key, rxthread_destructor);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_key_create, error code = %d", res);
		remove_posixshm();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	res = pthread_mutex_init(&posixshm_inst.thread_mtx, NULL);
	if(res != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_mutex_init, error code = %d", res);
		remove_posixshm();
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	add_itcthread(rc, posixshm_rx_thread, NULL, false, &posixshm_inst.thread_mtx);
}

static void init_posixshm_mempool(struct posixshm_metadata_t *shm_ptr)
{
	struct posixshm_metadata_t *metadata = shm_ptr;

	// At this moment owner don't need to lock my_sem_mutex since it's = 0 currently after initialization
	metadata->head.whichpool = -1;
	metadata->head.whichslot = -1;
	metadata->tail.whichpool = -1;
	metadata->tail.whichslot = -1;
	metadata->num_pages = POSIXSHM_STATIC_ALLOC_PAGES;
	metadata->num_unlimit_pages = 0;

	struct posixshm_slot_info_t *slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_96]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_96]; ++i)
	{
		slot_header->pool_type = POOL_96;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 128);
	}

	slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_480]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_480]; ++i)
	{
		slot_header->pool_type = POOL_480;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 512);
	}

	slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_992]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_992]; ++i)
	{
		slot_header->pool_type = POOL_992;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 1024);
	}

	slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_2016]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_2016]; ++i)
	{
		slot_header->pool_type = POOL_2016;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 2048);
	}

	slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_4064]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_4064]; ++i)
	{
		slot_header->pool_type = POOL_4064;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 4096);
	}

	slot_header = (struct posixshm_slot_info_t *)((unsigned long)metadata + posixshm_inst.pool_offset[POOL_16352]);
	for(unsigned char i = 0; i < posixshm_inst.num_slots[POOL_16352]; ++i)
	{
		slot_header->pool_type = POOL_16352;
		slot_header->is_in_use = false;
		slot_header->next_slot.whichpool = -1;
		slot_header->next_slot.whichslot = -1;
		slot_header = (struct posixshm_slot_info_t *)((unsigned long)slot_header + 16384);
	}
}

static void release_posixshm_resources(struct result_code* rc)
{
	remove_posixshm();
	remove_posixshm_semaphores();

	int ret = pthread_key_delete(posixshm_inst.destruct_key);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to pthread_key_delete, error code = %d", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	ret = pthread_mutex_destroy(&posixshm_inst.thread_mtx);
	if(ret != 0)
	{
		TPT_TRACE(TRACE_ERROR, "pthread_mutex_destroy error code = %d", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	posixshm_inst.is_terminated = 1;
	posixshm_inst.is_initialized = -1;
	memset(&posixshm_inst, 0, sizeof(struct posixshm_instance));
}



static struct posixshm_contactlist* get_posixshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	struct posixshm_contactlist* cl;

	cl = find_cl(rc, mbox_id);
	if(cl == NULL)
	{
		TPT_TRACE(TRACE_ERROR, "Contact list not found!");
		return NULL;
	}

	if(cl->mbox_id_in_itccoord == 0)
	{
		// TPT_TRACE(TRACE_INFO, "Add contact list!"); // TBD
		add_posixshm_cl(rc, cl, mbox_id);
	}

	return cl;
}

static struct posixshm_contactlist* find_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	itc_mbox_id_t pid;

	pid = (mbox_id & posixshm_inst.itccoord_mask) >> posixshm_inst.itccoord_shift;
	if(pid == 0 || pid >= MAX_SUPPORTED_PROCESSES)
	{
		TPT_TRACE(TRACE_ABN, "Invalid contact list's process id!");
		rc->flags |= ITC_INVALID_ARGUMENTS;
		return NULL;
	}

	return &(posixshm_inst.posixshm_cl[pid]);
}

static void add_posixshm_cl(struct result_code* rc, struct posixshm_contactlist* cl, itc_mbox_id_t mbox_id)
{
	int shmid;
	sem_t *sem_mutex;
	sem_t *sem_notify;
	sem_t *sem_slot;
	char partner_name[64];

	sprintf(partner_name, "/itc_posixshm_0x%08x", mbox_id & posixshm_inst.itccoord_mask);
	if ((shmid = shm_open(partner_name, O_RDWR, 0)) == -1)
	{
		TPT_TRACE(TRACE_ERROR, "Failed to shm_open partner posix shm %s, errno = %d!", partner_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sprintf(partner_name, "/itc_posixshm_sem_mutex_0x%08x", mbox_id & posixshm_inst.itccoord_mask);
	if ((sem_mutex = sem_open(partner_name, 0, 0, 0)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", partner_name, errno);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sprintf(partner_name, "/itc_posixshm_sem_notify_0x%08x", mbox_id & posixshm_inst.itccoord_mask);
	if ((sem_notify = sem_open(partner_name, 0, 0, 0)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", partner_name, errno);
		sem_close(sem_mutex);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sprintf(partner_name, "/itc_posixshm_sem_slot_0x%08x", mbox_id & posixshm_inst.itccoord_mask);
	if ((sem_slot = sem_open(partner_name, 0, 0, 0)) == SEM_FAILED) {
		TPT_TRACE(TRACE_ERROR, "Failed to sem_open, semaphore %s, errno = %d!", partner_name, errno);
		sem_close(sem_mutex);
		sem_close(sem_notify);
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	cl->mbox_id_in_itccoord = (mbox_id & posixshm_inst.itccoord_mask);
	cl->posixshm_id = shmid;
	cl->metadata = (struct posixshm_metadata_t *)mmap(NULL, POSIXSHM_STATIC_ALLOC_PAGES * POSIXSHM_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmid, 0);
	cl->m_sem_mutex = sem_mutex;
	cl->m_sem_notify = sem_notify;
	cl->m_sem_slot = sem_slot;
}

// static void remove_posixshm_cl(struct result_code* rc, itc_mbox_id_t mbox_id)
// {
// 	(void)rc;
// 	struct posixshm_contactlist* cl;

// 	cl = find_cl(rc, mbox_id);
// 	cl->mbox_id_in_itccoord = 0;
// 	cl->posix_mqd = 0;
// }

static void handle_received_message(struct posixshm_slot_info_t *slot)
{
	struct itc_message* message;
	struct itc_message* rxmsg;
	union itc_msg* msg;
	uint16_t flags;

	if(slot->is_in_use == false)
	{
		TPT_TRACE(TRACE_ERROR, "Slot is not in use, pool type = %u!", slot->pool_type);
		return;
	} else if(slot->pool_type > POOL_UNLIMIT)
	{
		TPT_TRACE(TRACE_ERROR, "Invalid pool type = %u!", slot->pool_type);
		return;
	}

	rxmsg = (struct itc_message *)((unsigned long)slot + POSIXSHM_SLOT_HEADER_SIZE);

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
}










