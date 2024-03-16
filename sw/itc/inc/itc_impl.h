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

#ifndef MAX_SUPPORTED_PROCESSES
#define	MAX_SUPPORTED_PROCESSES	255
#endif

#ifdef UNITTEST
#define ITC_NR_INTERNAL_USED_MBOXES 0 // Unittest for local trans so sock and sysvmq not used yet
#else
#define ITC_NR_INTERNAL_USED_MBOXES 2 // One for socket and one for sysvmq transports
#endif

#define CLZ(val) __builtin_clz(val)
#define CONVERT_TO_MESSAGE(msg) (struct itc_message*)((unsigned long)msg - ITC_HEADER_SIZE) // See itc_message in README

#define CONVERT_TO_MSG(message) (union itc_msg*)(&message->msgno)

#define MUTEX_LOCK(rc, lock)					\
	do							\
	{							\
		if(pthread_mutex_lock(lock) != 0)		\
		{						\
			rc->flags |= ITC_SYSCALL_ERROR;		\
		}						\
	} while(0)

#define MUTEX_UNLOCK(rc, lock)					\
	do							\
	{							\
		if(pthread_mutex_unlock(lock) != 0)		\
		{						\
			rc->flags |= ITC_SYSCALL_ERROR;		\
		}						\
	} while(0)

/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/
// Flags to see if you're itc_coordinator or not (used by itc_init() call)
#define ITC_FLAGS_I_AM_ITC_COOR 0x00000001
// Force to redo itc_init() for a process
#define ITC_FLAGS_FORCE_REINIT  0x00000100
// Indicate a message are in a rx queue of some mailbox.
#define ITC_FLAGS_MSG_INRXQUEUE 0x0001
// Normally, Linux allows us to have Real-time Processes's priority in range of 1-99, but it should be only 40. That's enough!
#define ITC_HIGH_PRIORITY	40

/*****************************************************************************\/
*****                            RETURN CODE                               *****
*******************************************************************************/
typedef enum {
	ITC_OK 				= 	0b0,			/* 0	- Everything good */
	ITC_ALREADY_USED  		= 	0b1,			/* 1	- The mailbox id already used by someone */
	ITC_ALREADY_INIT  		= 	0b10,			/* 2	- Already calling local_init() */
	ITC_NOT_INIT_YET  		=	0b100,			/* 4	- Not calling local_init() yet */
	ITC_OUT_OF_MEM    		=	0b1000,			/* 8	- Malloc return NULL due to not enough memory in heap */
	ITC_RX_QUEUE_NULL 		=	0b10000,		/* 16	- Not calling local_create_mailbox yet */
	ITC_RX_QUEUE_EMPTY		=	0b100000,		/* 32	- This is not really a problem at all */
	ITC_NOT_THIS_PROC		=	0b1000000,		/* 64	- Three highest hexes of mailbox id != my_mbox_id_in_itccoord */
	ITC_OUT_OF_RANGE		=	0b10000000,		/* 128	- Local_mb_id > nr_localmbx_datas */
	ITC_NOT_DEL_ALL_MBOX		=	0b100000000,		/* 256	- Not deleting all user mailboxes before itc_exit() */
	ITC_DEL_IN_WRONG_STATE		=	0b1000000000,		/* 512	- Delete a mailbox when it's not created yet */
	ITC_FREE_NULL_PTR		=	0b10000000000,		/* 1024	- Attempts to remove null qitem */
	ITC_INVALID_MAX_MSGSIZE		=	0b100000000000,		/* 2048	- Max_mallocsize < 0 or requested itc_msg size > max_mallocsize */
	ITC_SYSCALL_ERROR		=	0b1000000000000,	/* 4096	- System call return error: pthread, sysv message queue,... */
	ITC_INVALID_SCHED_PARAMS	=	0b10000000000000	/* 8192	- Invalid scheduling params */

} result_code_e;

struct result_code {
	int32_t flags;
};

typedef enum {
	MBOX_UNUSED,
	MBOX_INUSE,
	MBOX_NUM_STATES
} mbox_state;


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


#ifdef __cplusplus
}
#endif

#endif // __ITC_IMPL_H__