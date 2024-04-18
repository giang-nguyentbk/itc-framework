/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/


// Okay, first let's create an itc API declarations. Which functions we will offer to the end users.

#ifndef __ITC_H__
#define __ITC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>



/*****************************************************************************\/
*****                          VARIABLE MACROS                             *****
*******************************************************************************/
#define ITC_MAX_NAME_LENGTH 		255
#define ITC_MAX_MAILBOXES		65534
#define ITC_MAX_MAILBOXES_PER_THREAD	255

// If you make sure your mailbox's names you set later will be unique across the entire universe, you can use this flag
// for itc_init() call
#define ITC_NO_NAMESPACE	0x00000100
#define ITC_NO_MBOX_ID		0xFFFFFFFF
#define ITC_NO_WAIT		0
#define ITC_WAIT_FOREVER	-1
#define ITC_MY_MBOX_ID		0xFFF00000

/* Currently,
+ 0x90000100 - 0x90000500: For SYSV Message Queue
+ 0x90000500 - 0x90000A00: For SOCKET protocol (locate itccoord, over-host communication,...) */
#define ITC_MSG_BASE		0x90000000


/*****************************************************************************\/
*****                        ITC TYPE DECLARATIONS                         *****
*******************************************************************************/
typedef uint32_t itc_mbox_id_t;

typedef enum {
        ITC_INVALID_SCHEME = -1,
        ITC_MALLOC = 0,
        ITC_NUM_SCHEMES
} itc_alloc_scheme;

/*****************************************************************************\/
*****                        CORE API DECLARATIONS                         *****
*******************************************************************************/
/*
*  Initialize important infrastructure for ITC system (Only once per a process).
*  1. Need to be called once per process before any other ITC calls.
*  2. Specify how many mailboxes ("number_of_mailboxes") and pre-allocate them continuously in a block
*  (aligned with 16-byte, same as malloc work).
*  3. When user call itc_create_mailbox(), take an mailbox available from a block and give it to user.
*/
extern bool itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme,
                    uint32_t init_flags); // First usage is to see if itc_coord or not,
                                            // this is reserved for future usages.

/*
*  Release all ITC resources for the current process
*  This is only allowed if there is no active mailboxes being used by threads. This means that all used mailboxes
*  should be deleted by itc_delete_mailbox() first.
*/
extern bool itc_exit(void);

/*
*  Allocate an itc_msg 
*/
extern union itc_msg *itc_alloc(size_t size, uint32_t msgno);

/*
*  Deallocate an itc_msg 
*/
extern bool itc_free(union itc_msg **msg);

/*
*  Create a mailbox for the current thread.
*/
extern itc_mbox_id_t itc_create_mailbox(const char *name, uint32_t flags);

/*
*  Delete a mailbox for the current thread. You're only allowed to delete your own mailboxes in your thread.
*/
extern bool itc_delete_mailbox(itc_mbox_id_t mbox_id);

/*
*  Send an itc_msg
*/
extern bool itc_send(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace);

/*
*  Receive an itc_msg.
*       1. You can filter which message types you want to get. Param filter is an array with:
                filter[0] = how many message types you want to get.
                filter[1] = msgno1
                filter[2] = msgno2
                filter[3] = msgno3
                ...
        2. You can set timeout in miliseconds to let your thread be blocked to wait for messages.
        ITC_WAIT_FOREVER means wait forever until receiving any message and 0 means check the rx queue and return
        immediately no matter if there are messages or not. (in milisecond)
        3. You may want to get messages from someone only, or get from all mailboxes via ITC_FROM_ALL.

	Note that: 1 and 3 will be implemented in ITC V2.
*/
// extern union itc_msg *itc_receive(const uint32_t *filter, int32_t tmo, itc_mbox_id_t from);

/* We can easily use ITC_MY_MBOX_ID for "from" */
extern union itc_msg *itc_receive(int32_t tmo); // By default ITC V1 receiving the 1st message in the rx queue
						 			// no matter from who it came.



/*****************************************************************************\/
*****                       HELPER API DECLARATIONS                        *****
*******************************************************************************/
extern itc_mbox_id_t itc_sender(union itc_msg *msg);
extern itc_mbox_id_t itc_receiver(union itc_msg *msg);
extern size_t itc_size(union itc_msg *msg);
/* The first element of returned array is the number of active mailboxes in this thread, following elements respectively are those mailbox ids */
extern itc_mbox_id_t itc_current_mbox(void);

/*
*  Locate a mailbox across the entire universe.
*       1. First search for local mailboxes in the current process.
*       2. If cannot find, send a message ITC_LOCATE_MBOX_SYNC_REQ to itc_coord asking for seeking across processes.
*       3. If still cannot find, itc_coord will help send a message ITC_LOCATE_OVER_HOST to itc_gw asking for
*       broadcasting this message to all hosts on LAN network for locating the requested mailbox.
*
*       Note that: this may block your thread for some time, so please consider using itc_locate_async instead
*       if you're not sure the target mailbox is inside or outside your host.
*
*       Improvement: add one more input, let's say, uint32_t wheretofind. You're be able to select where to find
*       the target mailbox. Locally, or over processes, or even over hosts???
*/
extern itc_mbox_id_t itc_locate_sync(int32_t timeout, const char *name, bool find_only_internal, bool *is_external, char *namespace);
// extern itc_mbox_id_t itc_locate_sync(const char *name, uint32_t wheretofind);

/*
*  NOT IMPLEMENTED YET
*  Locate asynchronously a mailbox across the entire universe.
*       Same behaviour as itc_locate_sync() but you will give ITC system an itc_msg buffer **msg which will be filled
*       in when a mailbox is located, and ITC_LOCATE_NOT_FOUND if no mailbox is found. You also need to give your 
*       mailbox id to let ITC system send back to you
*/
// extern itc_mbox_id_t itc_locate_async(const char *name, union itc_msg **msg, itc_mbox_id_t from);

/*
*  Return file descriptor for the mailbox of the current thread.
*       Each thread has its own one itc mailbox. Each process (or the first default thread of a process) may have some
*       other mailboxes for socket, sysvmq, local_coordinator, which are shared for all threads.
*
*       Each mailbox has its own one eventfd instance which is used for notifying receiver regarding some message
*       has been sent to it. This is async mechanism for notification. Additionally, Pthread condition variable is
*       for sync mechanism instead.
*/
extern int itc_get_fd(itc_mbox_id_t mbox_id);

extern bool itc_get_name(itc_mbox_id_t mbox_id, char *name);

extern bool itc_get_namespace(int32_t timeout, char *namespace);

/*
*  NOT IMPLEMENTED YET
*  Monitor "alive" status of a mailbox.
*       1. By calling this function, you will register with the target mailbox. Right before the target mailbox is
*       deleted or its thread exits, target mailbox will send back a message to you to notify that.
*       2. You will manage two lists, one is monitored "alive" mailboxes, another is deleted mailboxes that
*       have notified you or mailboxes are unmonitored by itc_unmonitor() call.
*/
// extern itc_monitor_id_t itc_monitor(itc_mbox_id_t mbox_id, union itc_msg **msg);
// extern void itc_unmonitor(itc_mbox_id_t mbox_id);



/*****************************************************************************\/
*****                    MAP TO BACKEND IMPLEMENTATION                     *****
*******************************************************************************/
extern bool itc_init_zz(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, uint32_t init_flags);
#define itc_init(nr_mboxes, alloc_scheme, init_flags) itc_init_zz((nr_mboxes), (alloc_scheme), (init_flags))

extern bool itc_exit_zz(void);
#define itc_exit() itc_exit_zz()

extern union itc_msg *itc_alloc_zz(size_t size, uint32_t msgno);
#define itc_alloc(size, msgno) itc_alloc_zz((size), (msgno))

extern bool itc_free_zz(union itc_msg **msg);
#define itc_free(msg) itc_free_zz((msg))

extern itc_mbox_id_t itc_create_mailbox_zz(const char *name, uint32_t flags);
#define itc_create_mailbox(name, flags) itc_create_mailbox_zz((name), (flags))

extern bool itc_delete_mailbox_zz(itc_mbox_id_t mbox_id);
#define itc_delete_mailbox(mbox_id) itc_delete_mailbox_zz((mbox_id))

extern bool itc_send_zz(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace);
#define itc_send(msg, to, from, namespace) itc_send_zz((msg), (to), (from), (namespace))

extern union itc_msg *itc_receive_zz(int32_t tmo);
#define itc_receive(tmo) itc_receive_zz(tmo)

extern itc_mbox_id_t itc_sender_zz(union itc_msg *msg);
#define itc_sender(msg) itc_sender_zz((msg))

extern itc_mbox_id_t itc_receiver_zz(union itc_msg *msg);
#define itc_receiver(msg) itc_receiver_zz((msg))

extern size_t itc_size_zz(union itc_msg *msg);
#define itc_size(msg) itc_size_zz((msg))

extern itc_mbox_id_t itc_current_mbox_zz(void);
#define itc_current_mbox() itc_current_mbox_zz()

extern itc_mbox_id_t itc_locate_sync_zz(int32_t timeout, const char *name, bool find_only_internal, bool *is_external, char *namespace);
#define itc_locate_sync(timeout, name, find_only_internal, is_external, namespace) itc_locate_sync_zz((timeout), (name), (find_only_internal), (is_external), (namespace))

extern int itc_get_fd_zz(itc_mbox_id_t mbox_id);
#define itc_get_fd(mbox_id) itc_get_fd_zz(mbox_id)

extern bool itc_get_name_zz(itc_mbox_id_t mbox_id, char *name);
#define itc_get_name(mbox_id, name) itc_get_name_zz((mbox_id), (name))

extern bool itc_get_namespace_zz(int32_t timeout, char *name);
#define itc_get_namespace(timeout, name) itc_get_namespace_zz((timeout), (name))

#ifdef __cplusplus
}
#endif

#endif // __ITC_H__