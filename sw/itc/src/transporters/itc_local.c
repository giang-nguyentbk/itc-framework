/* Okay, let's put itc API declarations aside, we can easily start with an implementaion for local transportation
mechanism.

First, let's create an generic interface for transportation mechanisms. Our local trans is just one implementation of
that interface. To create a set of APIs in C, it's great because we can have a struct with a soft of function pointers
inside. Definitions for those function pointers (APIs) is implemented by local_trans, sock_trans or sysv_trans.

For example:
struct itci_trans_apis {
    itci_init               *itci_init;
    itci_create_mailbox     *itci_create_mailbox;
    itci_send               *itci_send;
    itci_receive            *itci_receive;
    ...
};

Which functions to be done:
    First have to implement above api functions.

    Additionally, we also need some private functions:
    + enqueue()
    + dequeue()
    + ...
*/


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                    INTERNAL TYPES IN LOCAL-ATOR                      *****
*******************************************************************************/
struct local_mbox_data {
        itc_mbox_id_t           mbox_id;
        uint32_t                flags;
        struct rxqueue        	*rxq;  // from itc_impl.h
};

struct local_instance {
        itc_mbox_id_t           my_mbox_id_in_itccoord;
        itc_mbox_id_t           itccoord_mask;
        itc_mbox_id_t           local_mbox_mask;

        uint32_t                     nr_localmbx_datas;
        struct local_mbox_data  *localmbx_data;
};



/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct local_instance local_inst; // One instance per a process, multiple threads all use this one.



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static struct local_mbox_data *find_localmbx_data(struct result_code* rc, itc_mbox_id_t mbox_id);
static void release_localmbx_resources(struct result_code* rc);
static struct rxqueue* init_queue(struct result_code* rc); // Used at mailbox creation to initialize rxqueue for the mailbox.
static void enqueue_message(struct result_code* rc, struct rxqueue* q, struct itc_message* message);
static struct itc_message* dequeue_message(struct result_code* rc, struct rxqueue *q);
// Will be implemented in ITC V2, below function is used for filtering out specific messages
// static struct itc_message* find_message_fromqueue(struct rxqueue* q, const uint32_t* filter, itc_mbox_id_t from);

/* Note that this function only find the message and remove its status "INQUEUE", not free() it */
/* Remember that deallocating a itc_msg is the responsibility of users who is expected that sender will call
   itc_alloc() -> itc_send() and receiver will call itc_receive() -> handle the message and itc_free() */
static struct itc_message* remove_message_fromqueue(struct result_code* rc, struct rxqueue* q, struct itc_message* message);
static struct llqueue_item* create_qitem(struct result_code* rc, struct itc_message* message);
static void remove_qitem(struct result_code* rc, struct llqueue_item** qitem);



/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void local_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags);

static void local_exit(struct result_code* rc);

static void local_create_mbox(struct result_code* rc, struct itc_mailbox *mailbox, uint32_t flags);

static void local_delete_mbox(struct result_code* rc, struct itc_mailbox *mailbox);

static void local_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static struct itc_message *local_receive(struct result_code* rc, struct itc_mailbox *my_mbox);

static struct itc_message *local_remove(struct result_code* rc, struct itc_mailbox *mbox, \
					struct itc_message *removed_message);

struct itci_transport_apis local_trans_apis = { NULL,
                                            	local_init,
                                            	local_exit,
                                            	local_create_mbox,
                                            	local_delete_mbox,
                                            	local_send,
                                            	local_receive,
                                            	local_remove,
                                            	NULL };



/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
static void local_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{       
        uint32_t mask, nr_localmb_data;

	LOG_INFO("ENTER local_init()!\n");
        /* If localmbx_data is not NULL, that means itc_init() was already run for this process. */
        if(local_inst.localmbx_data != NULL)
        {
                if(flags & ITC_FLAGS_FORCE_REINIT)
                {
			LOG_INFO("Force re-initializing!\n");
                        release_localmbx_resources(rc);
                } else
                {
			LOG_INFO("Already initialized!\n");
			rc->flags |= ITC_ALREADY_INIT;
                        return;
                }
        }

        /* If not initialised yet: */
        /*      1. Store the my_mbox_id_in_itccoord and itccoord_mask */
        local_inst.my_mbox_id_in_itccoord = my_mbox_id_in_itccoord;
        local_inst.itccoord_mask = itccoord_mask;

        /*      2. itc_init has allocated nr_mboxes in heap memory, we will allocate respective local_mbox_datas,
                but some more for reservation, you know, excess better than lack */
        /*      For example: if itc_init() already allocated 13 mailboxes -> then mask = 15
                (21->mask=31, 51->mask=63, 99->mask=127,...)
                So, we generously allocate local_mbox_datas up to mask + 1 for later uses */
        mask = 0xFFFFFFFF >> CLZ(nr_mboxes);
        local_inst.local_mbox_mask = mask;
        nr_localmb_data = mask + 1;

        local_inst.localmbx_data = (struct local_mbox_data *)malloc(nr_localmb_data*sizeof(struct local_mbox_data));
        if(local_inst.localmbx_data == NULL)
        {
                // Print a trace malloc() failed to allocate memory needed.
		LOG_ERROR("Failed to malloc local mbox data due to out of memory!\n");
		rc->flags |= ITC_SYSCALL_ERROR;
                return;
        }
        memset(local_inst.localmbx_data, 0, (nr_localmb_data*sizeof(struct local_mbox_data)));
        local_inst.nr_localmbx_datas = nr_localmb_data;
}

/* ITC infrastructure needs 2 more mailboxes for socket and sysv transports usage */
/* Users are expected to ensure they delete all mailboxes which were requested by themselve before calling itc_exit() */
static void local_exit(struct result_code* rc)
{
	struct local_mbox_data* lc_mb_data;
	uint32_t i, running_mboxes = 0;

	if(local_inst.localmbx_data == NULL)
	{
		// If not init yet, it's ok and just return, not a problem so not set ITC_NOT_INIT_YET here
		LOG_ABN("Not initialized yet, but it's ok to exit!\n");
		return;
	}

	for(i = 0; i < local_inst.nr_localmbx_datas; i++)
	{
		/* Go through all local mailbox's data */
		lc_mb_data = find_localmbx_data(rc, local_inst.my_mbox_id_in_itccoord | i);

		/* If NULL, not init yet or not belong to this process or mbox_id out of range */
		if(rc->flags != ITC_OK)
		{
			LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", local_inst.my_mbox_id_in_itccoord | i);
			return;
		}

		/* This local mailbox data slot was allocated for a mailbox via itc_create_mailbox() call */
		if(lc_mb_data->rxq != NULL)
		{
			running_mboxes++;
		}

		if(running_mboxes > ITC_NR_INTERNAL_USED_MBOXES)
		{
			// ERROR trace here needed, to let users know that they should delete their mailboxes first
			LOG_ABN("Still had %d remaining open mailboxes!\n", running_mboxes);
			rc->flags |= ITC_NOT_DEL_ALL_MBOX;
			return;
		}
	}

	free(local_inst.localmbx_data);
	local_inst.localmbx_data = NULL;
	memset(&local_inst, 0, sizeof(struct local_instance));
}

static void local_create_mbox(struct result_code* rc, struct itc_mailbox *mailbox, uint32_t flags)
{
	(void)flags;
	struct local_mbox_data* new_lc_mb_data;

	new_lc_mb_data = find_localmbx_data(rc, mailbox->mbox_id);
	if(rc->flags != ITC_OK)
	{
		// Not init yet, or not belong to this process or mbox_id out of range
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", mailbox->mbox_id);
		return;
	}

	if(new_lc_mb_data->rxq != NULL)
	{
		// Already in use by another mailbox, try another mailbox id instead
		LOG_ABN("Already used by another mailbox!\n");
		rc->flags |= ITC_ALREADY_USED;
		return;
	}

	new_lc_mb_data->rxq = init_queue(rc);
	if(rc->flags != ITC_OK)
	{
		// Failed to allocate new mailbox rx queue due to out of memory
		LOG_ERROR("Failed to init_queue!\n");
		return;
	}

	new_lc_mb_data->mbox_id = mailbox->mbox_id;
	new_lc_mb_data->flags = mailbox->flags;
}

static void local_delete_mbox(struct result_code* rc, struct itc_mailbox *mailbox)
{
	struct local_mbox_data* lc_mb_data;
	struct itc_message* message;
	union itc_msg* msg;

	lc_mb_data = find_localmbx_data(rc, mailbox->mbox_id);
	if(rc->flags != ITC_OK)
	{
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", mailbox->mbox_id);
		// Not init yet, or not belong to this process or mbox_id out of range
		return;
	}

	if(lc_mb_data->rxq == NULL)
	{
		LOG_ABN("Already used by another mailbox!\n");
		// Deleting a local mailbox data in wrong state
		rc->flags |= ITC_QUEUE_NULL;
		return;
	}

	/* Discard all messages in rx queue */
	while((message = dequeue_message(rc, lc_mb_data->rxq)) != NULL)
	{
		LOG_INFO("Discard a message from rx queue!\n");

/* This kind of preprocessor directive will be defined in Makefile with -DUNITTEST option for gcc/g++ compiler */
#ifdef UNITTEST
		/* Because in current implementation transport apis are using back its parent itc api definitions,
		   which is not good design. Luckily, itc_free() actually simple does call free() of stdlib.h,
		   so we can just call it here to simulate deallocating itc messages */
		free(message);
		(void)msg; // Avoid gcc compiler warning unused of msg in UNITTEST scenario.
#else
		msg = CONVERT_TO_MSG(message);
		itc_free(&msg);
#endif
	}

	// Delete a mailbox with empty rx queue is not a problem, so remove flag ITC_QUEUE_EMPTY
	// after dequeue_message()
	rc->flags &= ~ITC_QUEUE_EMPTY;

	free(lc_mb_data->rxq);
	lc_mb_data->rxq = NULL;

	lc_mb_data->mbox_id = 0;
	lc_mb_data->flags = 0;
}

static void local_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to)
{
	struct local_mbox_data* to_lc_mb_data;

	to_lc_mb_data = find_localmbx_data(rc, to);
	if(rc->flags != ITC_OK)
	{
		// Cannot find local mailbox data for this mailbox id in this process
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", to);
		return;
	}

	if(to_lc_mb_data->rxq == NULL)
	{
		// If q is NULL, that means the queue has not been initialized yet by init_queue()
		LOG_ABN("Already used by another mailbox!\n");
		rc->flags |= ITC_QUEUE_NULL;
		return;
	}

	enqueue_message(rc, to_lc_mb_data->rxq, message);
}

static struct itc_message *local_receive(struct result_code* rc, struct itc_mailbox *my_mbox)
{
	struct local_mbox_data* lc_mb_data;

	lc_mb_data = find_localmbx_data(rc, my_mbox->mbox_id);
	if(rc->flags != ITC_OK)
	{
		// Not init yet or not belong to this process or mbox_id out of range
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", my_mbox->mbox_id);
		return NULL;
	}

	if(lc_mb_data->rxq == NULL)
	{
		LOG_ABN("Already used by another mailbox!\n");
		rc->flags |= ITC_QUEUE_NULL;
		return NULL;
	}

	return dequeue_message(rc, lc_mb_data->rxq);
}

static struct itc_message *local_remove(struct result_code* rc, struct itc_mailbox *mbox, \
					struct itc_message *removed_message)
{
	struct local_mbox_data* lc_mb_data;


	lc_mb_data = find_localmbx_data(rc, mbox->mbox_id);
	if(rc->flags != ITC_OK)
	{
		// Not init yet or not belong to this process or mbox_id out of range
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x\n", mbox->mbox_id);
		return NULL;
	}

	if(lc_mb_data->rxq == NULL)
	{
		LOG_ABN("Already used by another mailbox!\n");
		rc->flags |= ITC_QUEUE_NULL;
		return NULL;
	}

	/* NULL (not found), or pointer to the removed message */
	/* Note again: remove status of the message from rx queue not meaning itc_free() the message */
	return remove_message_fromqueue(rc, lc_mb_data->rxq, removed_message);
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_localmbx_resources(struct result_code* rc)
{
        struct local_mbox_data* lc_mb_data;
        struct itc_message* message;
        union itc_msg* msg;
	uint32_t i = 0;

        for(i = 0; i < local_inst.nr_localmbx_datas; i++)
        {
		/* local_inst manages a list of mailboxes for all threads, so first search for our thread's mailbox */
                /* (local_inst.my_mbox_id_in_itccoord | i) -> our local mailbox id */
                lc_mb_data = find_localmbx_data(rc, local_inst.my_mbox_id_in_itccoord | i);

		/* No mailbox was created for this id, continue to the next one */
		if(lc_mb_data->rxq == NULL)
		{
			continue;
		}

                while((message = dequeue_message(rc, lc_mb_data->rxq)) != NULL)
                {
#ifdef UNITTEST
		free(message);
		(void)msg; // Avoid gcc compiler warning unused of msg in UNITTEST scenario.
#else
		msg = CONVERT_TO_MSG(message);
		itc_free(&msg);
#endif
                }

                free(lc_mb_data->rxq);
                lc_mb_data->rxq = NULL;
        }

        free(local_inst.localmbx_data);
	local_inst.localmbx_data = NULL;
	memset(&local_inst, 0, sizeof(struct local_instance));
}

static struct local_mbox_data* find_localmbx_data(struct result_code* rc, itc_mbox_id_t mbox_id)
{
	if(local_inst.localmbx_data == NULL)
	{
		LOG_ABN("Not initialized yet!\n");
		rc->flags |= ITC_NOT_INIT_YET;
		return NULL;
	}

	if((mbox_id & local_inst.itccoord_mask) != local_inst.my_mbox_id_in_itccoord)
	{
		LOG_ABN("Not belong to this process, mbox_id = 0x%08x, my_mbox_id_in_itccoord = 0x%08x\n", (mbox_id & local_inst.itccoord_mask), local_inst.my_mbox_id_in_itccoord);
		rc->flags |= ITC_NOT_THIS_PROC;
		return NULL;
	}

	/* Verify if the mbox_id belongs to this process or not out of range
	   AND mbox_id vs local_inst.local_mbox_mask was not working as expected
	   We couldn't get correctly local_mb_id
	   For example, mailbox_id = 0x0050000A with number of mailboxes was requested by local_init() is 5 mailboxes,
	   so the local_mask is 7 -> local_inst.nr_localmbx_datas = 8
	   But when AND 0x0050000A vs local_mask=7 -> we get local_mb_id=2 which is wrong, we expect it's 10
	   Therefore, masking mbox_id with ITC_MAX_MAILBOXES is a good workaround but not absolutely correct */
	if((mbox_id & ITC_MAX_MAILBOXES) >= local_inst.nr_localmbx_datas)
	{
		LOG_ABN("Mailbox ID exceeded nr_mboxes, local mbox_id = 0x%08x, nr_mboxes = %u!\n", (mbox_id & ITC_MAX_MAILBOXES), local_inst.nr_localmbx_datas);
		rc->flags |= ITC_OUT_OF_RANGE;
		return NULL;
	}
	
	return &(local_inst.localmbx_data[mbox_id & local_inst.local_mbox_mask]);
}

static struct rxqueue* init_queue(struct result_code* rc)
{
	struct rxqueue* retq;

	retq = (struct rxqueue*)malloc(sizeof(struct rxqueue));
	if(retq == NULL)
	{
		// Print out a ERROR trace here is needed.
		LOG_ERROR("Failed to malloc rxqueue due to out of memory!\n");
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	retq->head = NULL;
	retq->tail = NULL;
	retq->find = NULL;
	return retq;
}

static void enqueue_message(struct result_code* rc, struct rxqueue* q, struct itc_message* message)
{
	struct llqueue_item* new_qitem;

	new_qitem = create_qitem(rc, message);

	if(rc->flags != ITC_OK)
	{
		// create_qitem() failed due to out of memory
		LOG_ERROR("Failed to create a rx queue item!\n");
		return;
	}

	// Check if the queue tail is NULL or not.
	// If yes, so the queue now is empty, so move q->head and q->tail to the 1st item.
	// If not, just update the last item to point to new item and move q->tail to new item as well. 
	if(q->tail == NULL)
	{
		LOG_INFO("RX queue is empty, add a new one!\n");
		q->head = new_qitem;
		q->tail = new_qitem;
	} else
	{
		q->tail->next = new_qitem;
		new_qitem->prev = q->tail;
		q->tail = new_qitem;
	}

	new_qitem->msg_item->flags |= ITC_FLAGS_MSG_INRXQUEUE;
}

static struct itc_message* dequeue_message(struct result_code* rc, struct rxqueue *q)
{
	struct itc_message* message;

	// queue empty
	if(q->head == NULL)
	{
		// printf("\tDEBUG: dequeue_message - RX queue is empty!"); Will spam traces
		rc->flags |= ITC_QUEUE_EMPTY;
		return NULL;
	}

	message = q->head->msg_item;

	// In case queue has only one item
	if(q->head == q->tail)
	{
		LOG_INFO("RX queue has only one item, dequeue it!\n");
		remove_qitem(rc, &q->head);
		// Both head and tail should be moved to NULL
		q->head = NULL;
		q->tail = NULL;
	} else
	{
		// In case queue has more than one items, move head to the 2nd item and remove the 1st item via prev
		// pointer of the 2nd.
		q->head = q->head->next;
		remove_qitem(rc, &q->head->prev);
	}
	
	message->flags &= ~ITC_FLAGS_MSG_INRXQUEUE;

	return message;
}

static struct itc_message* remove_message_fromqueue(struct result_code* rc, struct rxqueue* q, struct itc_message* message)
{
	struct llqueue_item* iter, *prev = NULL;
	struct itc_message* ret = NULL;

	iter = q->head;
	/* Traverse the rx queue to find the llqueue_item that contains the message */
	while(iter != NULL)
	{
		if(iter->msg_item == message)
		{
			LOG_INFO("Item found!\n");
			break;
		}
		prev = iter;
		iter = iter->next;
	}

	if(iter != NULL)
	{
		/* The target item is the first one in queue */
		if(iter == q->head)
		{
			q->head = iter->next;
			/* if q->head = NULL meaning now queue is empty */
			if(q->head == NULL)
			{
				LOG_INFO("Queue empty!\n");
				q->tail = NULL;
			}
		} else
		{
			/* If not the first one, pull out our target item, concatenate prev to the next */
			prev->next = iter->next;
			/* If the target is the last one in queue, move tail back to prev */
			if(q->tail == iter)
			{
				LOG_INFO("Found one item which is the last one!\n");
				q->tail = prev;
			}
		}

		iter->msg_item->flags &= ~ITC_FLAGS_MSG_INRXQUEUE;

		ret = iter->msg_item;

		/* Clean up the removed queue item */
		remove_qitem(rc, &iter);

		return ret;
	}
	
	LOG_ABN("Message not found!\n");
	rc->flags |= ITC_QUEUE_EMPTY;

	/* If not found ret = NULL */
	return NULL;
}

static struct llqueue_item* create_qitem(struct result_code* rc, struct itc_message* message)
{
	struct llqueue_item* ret_qitem;

	ret_qitem = (struct llqueue_item*)malloc(sizeof(struct llqueue_item));
	if(ret_qitem == NULL)
	{
		// Print out an ERROR trace here is needed.
		LOG_ERROR("Failed to malloc linked list queue item for local mbox data due to out of memory!\n");
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	ret_qitem->msg_item = message;
	ret_qitem->next = NULL;
	ret_qitem->prev = NULL;

	return ret_qitem;
}

static void remove_qitem(struct result_code* rc, struct llqueue_item** qitem)
{
	if(qitem == NULL || *qitem == NULL)
	{
		LOG_ERROR("Double free!\n");
		rc->flags |= ITC_FREE_NULL_PTR;
		return;
	}

	free(*qitem);
	*qitem = NULL;
}

