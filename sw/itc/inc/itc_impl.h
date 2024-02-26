/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

#ifndef __ITC_IMPL_H__
#define __ITC_IMPL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "itc.h"

/*****************************************************************************\/
*****                     VARIABLE/FUNCTIONS MACROS                        *****
*******************************************************************************/
#define ENDPOINT (char)0xAA
#define ITC_HEADER_SIZE 14 // itc_message: flags + receiver + sender + size. Also is the offset between
                                // the starting of itc_message and the starting of itc_msg.

#define ITC_NR_INTERNAL_USED_MBOXES 2 // One for socket and one for sysv transports

#define CLZ(val) __builtin_clz(val)
#define CONVERT_TO_MESSAGE(msg) (struct itc_message*)((unsigned long)msg - ITC_HEADER_SIZE) // See itc_message in README

#define CONVERT_TO_MSG(message) (union itc_msg*)(&message->msgno)
/*****************************************************************************\/
*****                          VARIABLE MACROS                             *****
*******************************************************************************/



/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/
// Flags to see if you're itc_coordinator or not (used by itc_init() call)
#define ITC_FLAGS_I_AM_ITC_COOR 0x00000001
// Force to redo itc_init() for a process
#define ITC_FLAGS_FORCE_REINIT  0x00000100
// Indicate a message are in a rx queue of some mailbox.
#define ITC_FLAGS_MSG_INRXQUEUE 0x0001
/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/



/*****************************************************************************\/
*****                            RETURN CODE                               *****
*******************************************************************************/
// Result code used for allocator error handling
#define ITC_RET_OK                	0
// Already in use by another mailbox, try another mailbox id instead
#define ITC_RET_INIT_ALREADY_USED	-1
// itc_init() was already run for this process
#define ITC_RET_INIT_ALREADY_INIT       -2
// Not initialised yet or not belong to this process or mbox_id out of range
#define ITC_RET_INIT_NOT_INIT_YET	-3
// malloc() couldn't allocate memories due to out of memory
#define ITC_RET_INIT_OUT_OF_MEM         -4
/*****************************************************************************\/
*****                            RETURN CODE                               *****
*******************************************************************************/


/*****************************************************************************\/
*****                         TYPE DEFINITIONS                             *****
*******************************************************************************/
struct itc_mailbox {
        struct itc_mailbox          *next;
        uint32_t                    flags;

        uint32_t                    mbox_id;
        pid_t                       tid;
        char                        name[ITC_NAME_MAXLEN];
};

struct itc_message {
/* itc_message is only used for controlling itc system through below admin information,
do not access user data via itc_message but use itc_msg instead */
        uint16_t                    flags;

        /* DO NOT change anything in the remainder - this is a core part - to avoid breaking the whole ITC system. */
        itc_mbox_id_t               receiver;
        itc_mbox_id_t               sender;
        int32_t                     size;   // Size of the itc_msg that user gives to allocate itc_message

        /* This is the itc_msg part users can see and use it */
        uint32_t                    msgno;
        /* Starting of user data, optional */
        /* ...                             */

        /* There is one byte called endpoint which should be always 0xAA,
        if not, then there is something wrong with your itc message */
        /* Why it's 0xAA, because 0xAA = 1010 1010. Most efficient way to confirm the itc message correctness */
        /* char                     endpoint; */
};

struct llqueue_item {
	struct llqueue_item*	next;
	struct llqueue_item*	prev;

	/* Data of queue item, which points to an itc_message */
	struct itc_message*	msg_item;
};

/* Push into tail and pop from head */
struct rxqueue {
        struct llqueue_item* 	head;
        struct llqueue_item*	tail;
        struct llqueue_item*	find;
};
/*****************************************************************************\/
*****                         TYPE DEFINITIONS                             *****
*******************************************************************************/


#ifdef __cplusplus
}
#endif

#endif // __ITC_IMPL_H__