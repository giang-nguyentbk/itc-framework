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
        struct llrxqueue        *rxq;  // from itc_impl.h
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
static struct local_instance local_inst;

/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/


/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_localmbx_resources(void);
static struct local_mbox_data *find_localmbx_data(itc_mbox_id_t mbox_id);
static struct itc_message *dequeue_message(struct llrxqueue *queue);

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

static struct itc_message *local_receive(struct itc_mailbox  *mbox,
                                         const uint32_t      *filter,
                                         long                tmo,
                                         itc_mbox_id_t       from,
                                         bool                recursive_call);

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
                // Logging malloc() failed to allocate memory needed.
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

/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/

