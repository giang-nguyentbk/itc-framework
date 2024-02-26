/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

#ifndef __ITCI_TRANS_H__
#define __ITCI_TRANS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "itc.impl.h"
#include "itc.h"

// This is a function prototype
typedef bool (itci_trans_locate_coord)(itc_mbox_id_t *my_mbox_id_in_itccoord, itc_mbox_id_t  *itccoord_mask,
                                       itc_mbox_id_t *itccoord_mbox_id);

typedef int (itci_trans_init)(itc_mbox_id_t  my_mbox_id_in_itccoord,
                              itc_mbox_id_t  itccoord_mask,
                              int            nr_mboxes,
                              uint32_t       flags);

typedef int (itci_trans_exit)(void);

typedef int (itci_trans_create_mbox)(struct itc_mailbox *mailbox, uint32_t flags);

typedef int (itci_trans_delete_mbox)(struct itc_mailbox *mailbox);

typedef int (itci_trans_send)(struct itc_mailbox  *mbox,   
                              struct itc_message  *message,
                              itc_mbox_id_t       to,
                              itc_mbox_id_t       from);

typedef struct itc_message *(itci_trans_receive)(struct itc_mailbox *mbox, long timeout);

typedef struct itc_message *(itci_trans_remove)(struct itc_message *mailbox, struct itc_message *removemessage);

typedef int (itci_trans_maxmsgsize)(void);

/*
*  1. Local trans: implemented as a rx message queue for each mailbox. Only manage message passing within a process and
*     create/delete mailboxes, not used for locating itc_coord.
*  2. Socket trans: only used for locating itc_coord purposes and send large messages over processes
*     that sysv could not carry.
*  3. Sysv trans: only used for sending messages over processes.
*/
struct itci_transport_apis {
        itci_trans_locate_coord          *itci_trans_locate_itccoord;     // API to locate the itc_coord which will
                                                                        // be replied by itc_coord with
                                                                        // your mbox id in itc_coord, mask
                                                                        // and itc_coord's mbox id respectively.

        itci_trans_init                 *itci_trans_init;               // API to setup all initial configuration
                                                                        // when ITC system is initialized for a process

        itci_trans_exit                 *itci_trans_exit;               // API to release all above configuration
        itci_trans_create_mbx           *itci_trans_create_mbox;         // API to initialize necessary stuffs
                                                                        // (init a local trans's rx queue)
                                                                        // when a mailbox is created

        itci_trans_delete_mbx           *itci_trans_delete_mbox;         // API to release stuffs at mailbox deletion
        itci_trans_send                 *itci_trans_send;               // API to send a message
        itci_trans_receive              *itci_trans_receive;            // API to receive a message
        itci_trans_remove               *itci_trans_remove;             // API to remove a message from rx queue
        itci_trans_maxmsgsize           *itci_trans_maxmsgsize;         // API to get max supported msgsize
};


#ifdef __cplusplus
}
#endif

#endif // __ITCI_TRANS_H__