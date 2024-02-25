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
#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"


/*****************************************************************************\/
*****                    INTERNAL TYPES IN LOCAL-ATOR                      *****
*******************************************************************************/
struct local_mbox_data {
        itc_mbox_id_t           mbox_id;
        uint32_t                flags;
        struct rxqueue        *rxq;  // from itc_impl.h
};

struct local_instance {
        itc_mbox_id_t           my_mbox_id_in_itccoord;
        itc_mbox_id_t           itccoord_mask;
        itc_mbox_id_t           local_mbox_mask;

        int                     nr_localmbx_datas;
        struct local_mbox_data  *localmbx_data;
};

/*****************************************************************************\/
*****                    INTERNAL TYPES IN LOCAL-ATOR                      *****
*******************************************************************************/



/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct local_instance local_inst; // One instance per a process, multiple threads all use this one.

/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/


/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_localmbx_resources(void);
static struct local_mbox_data *find_localmbx_data(itc_mbox_id_t mbox_id);

static rxqueue* init_queue(void); // Used at mailbox creation to initialize rxqueue for the mailbox.
static void enqueue_message(struct rxqueue* q, struct itc_message* message);
static struct itc_message* dequeue_message(struct rxqueue *q);
// Will be implemented in ITC V2
// static struct itc_message* find_message_fromqueue(struct rxqueue* q, const uint32_t* filter, itc_mbox_id_t from);

/* Note that this function only find the message and remove its status "INQUEUE", not free() it */
/* Remember that deallocating a itc_msg is the responsibility of users who is expected that sender will call
   itc_alloc() -> itc_send() and receiver will call itc_receive() -> handle the message and itc_free() */
static struct itc_message* remove_message_fromqueue(struct rxqueue* q, itc_message* message);
static struct llqueue_item* create_qitem(struct itc_message* message);
static void remove_qitem(struct llqueue_item* qitem);


/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/



/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static int local_init(itc_mbox_id_t  my_mbox_id_in_itccoord,
                      itc_mbox_id_t   itccoord_mask,
                      int             nr_mboxes,
                      uint32_t        flags);

static int local_exit(void);

static int local_create_mbox(struct itc_mailbox  *mailbox,
                             uint32_t      flags);

static int local_delete_mbox(struct itc_mailbox  *mailbox);

static int local_send(struct itc_mailbox  *mbox,   
                      struct itc_message  *message,
                      itc_mbox_id_t       to,
                      itc_mbox_id_t       from);

static struct itc_message *local_receive(struct itc_mailbox  *mbox);

static struct itc_message *local_remove(struct itc_message  *mailbox,
                                        struct itc_message  *removemessage);

struct itci_trans_apis local_trans_apis = { NULL,
                                            local_init,
                                            local_exit,
                                            local_create_mbox,
                                            local_delete_mbox,
                                            local_send,
                                            local_receive,
                                            local_remove,
                                            NULL };
/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/



/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
static int local_init(itc_mbox_id_t  my_mbox_id_in_itccoord,
                     itc_mbox_id_t  itccoord_mask,
                     int            nr_mboxes,
                     uint32_t       flags)
{       
        uint32_t mask, nr_localmb_data;
        
        /* If localmbx_data is not NULL, that means itc_init() was already run for this process. */
        if(local_inst.localmbx_data != NULL)
        {
                if(flags & ITC_FLAGS_FORCE_REINIT)
                {
                        release_localmbx_resources();
                } else
                {
                        return ITC_RET_INIT_ALREADY_INIT;
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
                return ITC_RET_INIT_OUT_OF_MEM;
        }
        memset(local_inst.localmbx_data, 0, (nr_localmb_data*sizeof(struct local_mbox_data)));
        local_inst.nr_localmbx_datas = nr_localmb_data;

        return ITC_RET_OK;
}



/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/



/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_localmbx_resources(void)
{
        struct local_mbox_data *lc_mb_data;
        struct itc_message *message;
        union itc_msg *msg;

        for(int i=0; i < local_inst.nr_localmbx_datas; i++)
        {
		/* local_inst manages a list of mailboxes for all threads, so first search for our thread's mailbox */
                /* (local_inst.my_mbox_id_in_itccoord | i) -> our local mailbox id */
                lc_mb_data = find_localmbx_data(local_inst.my_mbox_id_in_itccoord | i);

                while((message = dequeue_message(lc_mb_data->rxq)) != NULL)
                {
                        msg = CONVERT_TO_MSG(message);
                        itc_free(&msg);
                }

                free(lc_mb_data->rxq);
                lc_mb_data->rxq = NULL;
        }

        free(local_inst.localmbx_data);
        memset(&local_inst.localmbx_data, 0, sizeof(struct local_instance));
}

static struct local_mbox_data *find_localmbx_data(itc_mbox_id_t mbox_id)
{
	if(local_inst.localmbx_data == NULL)
	{
		return NULL;
	}

	/* Verify if the mbox_id belongs to this process */
	if(mbox_id & local_inst.itccoord_mask == local_inst.my_mbox_id_in_itccoord)
	{
		return &(local_inst.localmbx_data[mbox_id & local_inst.local_mbox_mask]);
	}

	return NULL;
}

static rxqueue* init_queue(void)
{
	struct rxqueue* retq;

	retq = (struct rxqueue*)malloc(sizeof(struct rxqueue));
	if(retq == NULL)
	{
		// Print out a ERROR trace here is needed.
		return NULL;
	}

	retq->head = NULL;
	retq->tail = NULL;
	retq->find = NULL;
	return retq;
}

static void enqueue_message(struct rxqueue* q, struct itc_message* message)
{
	// If q is NULL, that means the queue has not been initialized yet by init_queue()
	if(q == NULL)
	{
		return;
	}

	struct llqueue_item* new_qitem;

	new_qitem = create_qitem(message);

	// Check if the queue tail is NULL or not.
	// If yes, so the queue now is empty, so move q->head and q->tail to the 1st item.
	// If not, just update the last item to point to new item and move q->tail to new item as well. 
	if(q->tail == NULL)
	{
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

static struct itc_message* dequeue_message(struct rxqueue *q)
{
	struct llqueue_item* item;
	struct itc_message* message;

	// queue not initialized yet
	if(q == NULL)
	{
		return NULL;
	}

	// queue empty
	if(q->head == NULL)
	{
		return NULL;
	}

	message = q->head->msg_item;

	// In case queue has only one item
	if(q->head == q->tail)
	{
		remove_qitem(q->head);
		// Both head and tail should be moved to NULL
		q->head = NULL;
		q->tail = NULL;
	} else
	{
		// In case queue has more than one items, move head to the 2nd item and remove the 1st item via prev
		// pointer of the 2nd.
		q->head = q->head->next;
		remove_qitem(q->head->prev);
	}
	
	message->flags &= ~ITC_FLAGS_MSG_INRXQUEUE;

	return message;
}

static struct itc_message* remove_message_fromqueue(struct rxqueue* q, itc_message* message)
{

}

static struct llqueue_item* create_qitem(struct itc_message* message)
{
	struct llqueue_item* ret_qitem;

	ret_qitem = (struct llqueue_item*)malloc(sizeof(struct llqueue_item));
	if(ret_qitem == NULL)
	{
		// Print out a ERROR trace here is needed.
		return NULL;
	}

	ret_qitem->msg_item = message;
	ret_qitem->next = NULL;
	ret_qitem->prev = NULL;

	return ret_qitem;
}

static void remove_qitem(struct llqueue_item* qitem)
{
	free(qitem);
}

/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/

