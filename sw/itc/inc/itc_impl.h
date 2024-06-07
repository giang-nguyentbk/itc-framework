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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#include "itc.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                     VARIABLE/FUNCTIONS MACROS                        *****
*******************************************************************************/
#define ENDPOINT (char)0xAA
#define ITC_HEADER_SIZE 16 // itc_message: flags + receiver + sender + size. Also is the offset between
                                // the starting of itc_message and the starting of itc_msg.
#define ITC_MAX_MSGSIZE	(1024*1024)

#define ITC_COORD_MASK			0xFFF00000
#define ITC_COORD_SHIFT			20
#define ITC_COORD_MBOX_NAME		"itc_coord_mailbox"
#define ITC_GATEWAY_MBOX_UDP_NAME	"itc_gw_udp_mailbox"
#define ITC_GATEWAY_MBOX_TCP_CLI_NAME	"itcgw_tcpclient_mailbox"
#define ITC_GATEWAY_MBOX_TCP_SER_NAME	"itcgw_tcpserver_mailbox"
#define ITC_GATEWAY_BROADCAST_PORT	11111
#define ITC_GATEWAY_TCP_LISTENING_PORT	22222
#define ITC_GATEWAY_BROADCAST_INTERVAL	10
#define ITC_GATEWAY_ETH_PACKET_SIZE	1500
#define ITC_GATEWAY_NET_INTERFACE_ETH0	"eth0"
#define ITC_GATEWAY_NET_INTERFACE_LO	"lo"
#define ITC_GATEWAY_NO_ADDR_STRING	"NO_ADDR"
#define ITC_GATEWAY_MAX_PEERS		255

#define MAX_OF(a, b)		(a) > (b) ? (a) : (b)
#define MIN_OF(a, b)		(a) < (b) ? (a) : (b)

#ifndef MAX_SUPPORTED_PROCESSES
#define	MAX_SUPPORTED_PROCESSES	255
#endif

#ifndef ITC_BASE_PATH
#define ITC_BASE_PATH 			"/tmp/itc/"
#endif

#ifndef ITC_ITCCOORD_FOLDER
#define ITC_ITCCOORD_FOLDER 		"/tmp/itc/itccoord/"
#endif

#ifndef ITC_ITCCOORD_FILENAME
#define ITC_ITCCOORD_FILENAME 		"/tmp/itc/itccoord/itc_coordinator"
#endif

#ifndef ITC_SOCKET_FOLDER
#define ITC_SOCKET_FOLDER 		"/tmp/itc/socket/"
#endif

#ifndef ITC_LSOCKET_FILENAME
#define ITC_LSOCKET_FILENAME 		"/tmp/itc/socket/lsocket"
#endif

#ifndef ITC_SYSVMSQ_FOLDER
#define ITC_SYSVMSQ_FOLDER 		"/tmp/itc/sysvmsq/"
#endif

#ifndef ITC_SYSVMSQ_FILENAME
#define ITC_SYSVMSQ_FILENAME 		"/tmp/itc/sysvmsq/sysvmsq_file"
#endif

#ifndef ITC_ITCGWS_LOGFILE
#define ITC_ITCGWS_LOGFILE 		"itcgws.log"
#endif

#ifndef ITC_ITCCOORD_LOGFILE
#define ITC_ITCCOORD_LOGFILE 		"itccoord.log"
#endif

#ifdef LOCAL_TRANS_UNITTEST
#define ITC_NR_INTERNAL_USED_MBOXES 0 // Unittest for local trans so sock and sysvmq not used yet
#else
#define ITC_NR_INTERNAL_USED_MBOXES 1 // For sysvmq_rx_thread
#endif

#define CLZ(val) __builtin_clz(val)
#define CONVERT_TO_MESSAGE(msg) (struct itc_message*)((unsigned long)msg - ITC_HEADER_SIZE) // See itc_message in README

#define CONVERT_TO_MSG(message) (union itc_msg*)(&message->msgno)

#define CHECK_RC_EXIT(rc)				\
	do						\
	{						\
		if((rc->flags) != ITC_OK)		\
		{					\
			exit(EXIT_FAILURE);		\
		}					\
	} while(0)	


unsigned long int calc_time_diff(struct timespec t_start, struct timespec t_end);

#if defined MUTEX_TRACE_TIME_UNITTEST
/* Tracing prolong locking mutex, > 5ms */
#define MUTEX_LOCK(lock)									\
	do											\
	{											\
		struct timespec t_start;							\
		struct timespec t_end;								\
		clock_gettime(CLOCK_MONOTONIC, &t_start);					\
		pthread_mutex_lock(lock);							\
		clock_gettime(CLOCK_MONOTONIC, &t_end);						\
		unsigned long int difftime = calc_time_diff(t_start, t_end);			\
		if(difftime/1000000 > 5)							\
		{										\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK\t0x%08lx,\t%s:%d,\t"			\
				"time_elapsed = %lu (ms)!\n",					\
				(unsigned long)lock, __FILE__, __LINE__, difftime/1000000);	\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK - t_start.tv_sec = %lu!", 		\
				t_start.tv_sec);						\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK - t_start.tv_nsec = %lu!", 		\
				t_start.tv_nsec);						\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK - t_end.tv_sec = %lu!", 		\
				t_end.tv_sec);							\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK - t_end.tv_nsec = %lu!",		\
				t_end.tv_nsec);							\
		}										\
	} while(0)

#define MUTEX_UNLOCK(lock)									\
	do											\
	{											\
		struct timespec t_start;							\
		struct timespec t_end;								\
		clock_gettime(CLOCK_MONOTONIC, &t_start);					\
		pthread_mutex_unlock(lock);							\
		clock_gettime(CLOCK_MONOTONIC, &t_end);						\
		unsigned long int difftime = calc_time_diff(t_start, t_end);			\
		if(difftime/1000000 > 5)							\
		{										\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK\t0x%08lx,\t%s:%d,\t"		\
				"time_elapsed = %lu (ms)!\n",					\
				(unsigned long)lock, __FILE__, __LINE__, difftime/1000000);	\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK - t_start.tv_sec = %lu!", 		\
				t_start.tv_sec);						\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK - t_start.tv_nsec = %lu!", 	\
				t_start.tv_nsec);						\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK - t_end.tv_sec = %lu!", 		\
				t_end.tv_sec);							\
			TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK - t_end.tv_nsec = %lu!",		\
				t_end.tv_nsec);							\
		}										\
	} while(0)
#elif defined MUTEX_TRACE_UNITTEST
#define MUTEX_LOCK(lock)								\
	do										\
	{										\
		TPT_TRACE(TRACE_DEBUG, "MUTEX_LOCK\t0x%08lx,\t%s:%d", 			\
			(unsigned long)lock, __FILE__, __LINE__);			\
		pthread_mutex_lock(lock);						\
	} while(0)

#define MUTEX_UNLOCK(lock)								\
	do										\
	{										\
		TPT_TRACE(TRACE_DEBUG, "MUTEX_UNLOCK\t0x%08lx,\t%s:%d", 		\
			(unsigned long)lock, __FILE__, __LINE__);			\
		pthread_mutex_unlock(lock);						\
	} while(0)
#else
#define MUTEX_LOCK(lock)								\
	do										\
	{										\
		pthread_mutex_lock(lock);						\
	} while(0)

#define MUTEX_UNLOCK(lock)								\
	do										\
	{										\
		pthread_mutex_unlock(lock);						\
	} while(0)
#endif

#define MIN(x, y)	((x) < (y)) ? (x) : (y)
#define MAX(x, y)	((x) > (y)) ? (x) : (y)

/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/
// Flags to see if you're itc_coordinator or not (used by itc_init() call)
#define ITC_FLAGS_I_AM_ITC_COORD 0x00000001
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
	ITC_OK 				= 	0b0,			/* 0		- Everything good */
	ITC_ALREADY_USED  		= 	0b1,			/* 1		- The mailbox id already used by someone */
	ITC_ALREADY_INIT  		= 	0b10,			/* 2		- Already calling local_init() */
	ITC_NOT_INIT_YET  		=	0b100,			/* 4		- Not calling local_init() yet */
	ITC_QUEUE_NULL 			=	0b1000,			/* 8		- Not calling local_create_mailbox yet */
	ITC_QUEUE_EMPTY			=	0b10000,		/* 16		- This is not really a problem at all */
	ITC_NOT_THIS_PROC		=	0b100000,		/* 32		- Three highest hexes of mailbox id != my_mbox_id_in_itccoord */
	ITC_OUT_OF_RANGE		=	0b1000000,		/* 64		- Local_mb_id > nr_localmbx_datas */
	ITC_NOT_DEL_ALL_MBOX		=	0b10000000,		/* 128		- Not deleting all user mailboxes before itc_exit() */
	ITC_DEL_IN_WRONG_STATE		=	0b100000000,		/* 256		- Delete a mailbox when it's not created yet */
	ITC_FREE_NULL_PTR		=	0b1000000000,		/* 512		- Attempts to remove null qitem */
	ITC_INVALID_ARGUMENTS		=	0b10000000000,		/* 1024		- Validation of function parameters is invalid */
	ITC_SYSCALL_ERROR		=	0b100000000000,		/* 2048		- System call return error: pthread, sysv message queue,... */
	ITC_INVALID_MSG_SIZE		=	0b1000000000000,	/* 4096		- Message length received too large or short */
	ITC_INVALID_RESPONSE		=	0b10000000000000	/* 8192 	- Received an invalid response */
} result_code_e;

struct result_code {
	uint32_t flags;
};

typedef enum {
	MBOX_UNUSED,
	MBOX_INUSE,
	MBOX_NUM_STATES
} mbox_state_e;

typedef	enum {
	ITC_INVALID_TRANS = -1,
	ITC_TRANS_LOCAL	= 0,
	ITC_TRANS_SYSVMQ,
	ITC_TRANS_LSOCK,
	ITC_NUM_TRANS
} itc_transport_e;

// Because currently allocation scheme using malloc does not need any special parameters for allocation. We will reserve it for future usages.
struct itc_malloc_scheme {
        unsigned int reserved;
};

union itc_scheme {
        struct itc_malloc_scheme        malloc_scheme;
};

struct itc_malloc_info {
        long    max_msgsize;    // Give users information regarding max size of itc_msg can be used
                                // for the current malloc allocator, users can consider to optimize itc_msg
                                // or some other ways if their itc_msg's length is too large.
};

struct itc_alloc_info {
        itc_alloc_scheme scheme;

        union {
                struct itc_malloc_info          malloc_info;
                // struct itc_pool_info            pool_info;
                // struct itc_poolflex_info        poolflex_info;
        } info;
};

/*****************************************************************************\/
*****                         TYPE DEFINITIONS                             *****
*******************************************************************************/
struct mbox_rxq_info {
	pthread_mutexattr_t		rxq_attr;
	pthread_mutex_t			rxq_mtx;
	pthread_cond_t			rxq_cond;

	int				rxq_fd;
	bool				is_fd_created;
	long				rxq_len;
	bool				is_in_rx;
};

struct itc_mailbox {
        uint32_t                    	flags;
	struct mbox_rxq_info		rxq_info;
	struct mbox_rxq_info*		p_rxq_info;

        uint32_t                    	mbox_id;
	mbox_state_e			mbox_state;
        pid_t                       	tid;
        char                        	name[ITC_MAX_NAME_LENGTH];
};

struct itc_message {
/* Any change in itc_message struct size must lead to re-calculation of ITC_HEADER_SIZE as well */
/* itc_message is only used for controlling itc system through below admin information,
do not access user data via itc_message but use itc_msg instead */
        uint32_t               	flags;

        /* DO NOT change anything in the remainder - this is a core part - to avoid breaking the whole ITC system. */
        itc_mbox_id_t          	receiver;
        itc_mbox_id_t          	sender;
        uint32_t                size;   // Size of the itc_msg that user gives to allocate itc_message

        /* This is the itc_msg part users can see and use it */
        uint32_t               	msgno;
        /* Starting of user data, optional */
        /* ...                             */

        /* There is one byte called endpoint which should be always 0xAA,
        if not, then there is something wrong with your itc message */
        /* Why it's 0xAA, because 0xAA = 1010 1010. Most efficient way to confirm the itc message correctness */
        // char                     payload_startpoint[1]; // Actual user data will start from this byte address

	/* Payload in bytes
	*  ....
	*  Payload in bytes */

	/* char			endpoint; // Will be 0xAA */
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