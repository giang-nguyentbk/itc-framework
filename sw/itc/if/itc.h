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
#include <stddef.h>
#include <sys/types.h>



/*****************************************************************************\/
*****                          VARIABLE MACROS                             *****
*******************************************************************************/
#define ITC_NAME_MAXLEN 255

// If you make sure your mailbox's names you set later will be unique across the entire universe, you can use this flag
// for itc_init() call
#define ITC_NO_NAMESPACE NULL
/*****************************************************************************\/
*****                          VARIABLE MACROS                             *****
*******************************************************************************/



/*****************************************************************************\/
*****                        ITC TYPE DECLARATIONS                         *****
*******************************************************************************/
typedef uint32_t itc_mbox_id_t;

typedef enum {
	MBOX_UNUSED,
	MBOX_INUSE,
	MBOX_NUM_STATES
} mbox_state;

// Because currently allocation scheme using malloc does not need any special parameters for allocation.
// We will reserve it for future usages.
struct itc_malloc_scheme {
        unsigned int reserved;
};

union itc_scheme {
        struct itc_malloc_scheme        malloc_scheme;
};

typedef enum {
        ITC_INVALID_SCHEME = -1,
        ITC_MALLOC = 0,
        ITC_POOL = 1,
        ITC_NUM_SCHEMES
} itc_alloc_scheme;

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
*****                        ITC TYPE DECLARATIONS                         *****
*******************************************************************************/



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
extern int itc_init(int32_t nr_mboxes,
                    itc_alloc_scheme alloc_scheme,
                    union itc_scheme *scheme_params,
                    char *namespace, // Will be implemented later
                    uint32_t init_flags); // First usage is to see if itc_coord or not,
                                            // this is reserved for future usages.

/*
*  Release all ITC resources for the current process
*  This is only allowed if there is no active mailboxes being used by threads. This means that all used mailboxes
*  should be deleted by itc_delete_mailbox() first.
*/
extern void itc_exit(void);

/*
*  Allocate an itc_msg 
*/
extern union itc_msg *itc_alloc(size_t size, uint32_t msgno);

/*
*  Deallocate an itc_msg 
*/
extern void itc_free(union itc_msg **msg);

/*
*  Create a mailbox for the current thread.
*/
extern itc_mbox_id_t itc_create_mailbox(const char *name, uint32_t flags);

/*
*  Delete a mailbox for the current thread. You're only allowed to delete your own mailboxes in your thread.
*/
extern void itc_delete_mailbox(itc_mbox_id_t mbox_id);

/*
*  Send an itc_msg
*/
extern void itc_send(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from);

/*
*  Receive an itc_msg.
*       1. You can filter which message types you want to get. Param filter is an array with:
                filter[0] = how many message types you want to get.
                filter[1] = msgno1
                filter[2] = msgno2
                filter[3] = msgno3
                ...
        2. You can set timeout in miliseconds to let your thread be blocked to wait for messages.
        ITC_NO_TMO means wait forever until receiving any message and 0 means check the rx queue and return
        immediately no matter if there are messages or not.
        3. You may want to get messages from someone only, or get from all mailboxes via ITC_FROM_ALL.

	Note that: 1 and 3 will be implemented in ITC V2.
*/
// extern union itc_msg *itc_receive(const uint32_t *filter, uint32_t tmo,itc_mbox_id_t from);
extern union itc_msg *itc_receive(uint32_t tmo); // By default ITC V1 receiving the 1st message in the rx queue
						 // no matter from who it came.

/*****************************************************************************\/
*****                        CORE API DECLARATIONS                         *****
*******************************************************************************/



/*****************************************************************************\/
*****                       HELPER API DECLARATIONS                        *****
*******************************************************************************/
extern itc_mbox_id_t itc_sender(union itc_msg *msg);
extern itc_mbox_id_t itc_receiver(union itc_msg *msg);
extern size_t itc_size(union itc_msg *msg);
extern itc_mbox_id_t itc_current_mbox(void);

/*
*  Locate a mailbox across the entire universe.
*       1. First search for local mailboxes in the current process.
*       2. If cannot find, send a message ITC_LOCATE_OVER_PROC to itc_coord asking for seeking across processes.
*       3. If still cannot find, itc_coord will help send a message ITC_LOCATE_OVER_HOST to itc_gw asking for
*       broadcasting this message to all hosts on LAN network for locating the requested mailbox.
*
*       Note that: this may block your thread for some time, so please consider using itc_locate_async instead
*       if you're not sure the target mailbox is inside or outside your host.
*
*       Improvement: add one more input, let's say, uint32_t wheretofind. You're be able to select where to find
*       the target mailbox. Locally, or over processes, or even over hosts???
*/
extern itc_mbox_id_t itc_locate_sync(const char *name, uint32_t wheretofind);

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
*       other mailboxes for socket, sysv, local_coordinator, which are shared for all threads.
*
*       Each mailbox has its own one eventfd instance which is used for notifying receiver regarding some message
*       has been sent to it. This is async mechanism for notification. Additionally, Pthread condition variable is
*       for sync mechanism instead.
*/
extern int itc_get_fd(void);

extern int32_t itc_get_name(itc_mbox_id_t mbox_id, char *name, uint32_t name_len);

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
*****                       HELPER API DECLARATIONS                        *****
*******************************************************************************/


/*****************************************************************************\/
*****                    MAP TO BACKEND IMPLEMENTATION                     *****
*******************************************************************************/
extern int itc_init_zz(int32_t nr_mboxes,
                    itc_alloc_scheme alloc_scheme,
                    union itc_scheme *scheme_params,
                    char *namespace,
                    uint32_t init_flags);
#define itc_init(nr_mboxes, alloc_scheme, scheme_params, namespace, init_flags) \
                itc_init_zz((nr_mboxes), (alloc_scheme), (scheme_params), (namespace), (init_flags))


extern void itc_exit_zz(void);
#define itc_exit() itc_exit_zz()

extern union itc_msg *itc_alloc_zz(size_t size, uint32_t msgno);
#define itc_alloc(size, msgno) itc_alloc_zz((size), (msgno))

extern void itc_free_zz(union itc_msg **msg);
#define itc_free(msg) itc_free_zz((msg))

extern itc_mbox_id_t itc_create_mailbox_zz(const char *name, uint32_t flags);
#define itc_create_mailbox(name, flags) itc_create_mailbox_zz((name), (flags))

extern void itc_delete_mailbox_zz(itc_mbox_id_t mbox_id);
#define itc_delete_mailbox(mbox_id) itc_delete_mailbox_zz((mbox_id))

extern void itc_send_zz(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from);
#define itc_send(msg, to, from) itc_send_zz((msg), (to), (from))

extern union itc_msg *itc_receive_zz(uint32_t tmo);
#define itc_receive(tmo) itc_receive_zz(tmo)

extern itc_mbox_id_t itc_sender_zz(union itc_msg *msg);
#define itc_sender(msg) itc_sender_zz((msg))

extern itc_mbox_id_t itc_receiver_zz(union itc_msg *msg);
#define itc_receiver(msg) itc_receiver_zz((msg))

extern size_t itc_size_zz(union itc_msg *msg);
#define itc_size(msg) itc_size_zz((msg))

extern itc_mbox_id_t itc_current_mbox_zz(void);
#define itc_current_mbox() itc_current_mbox_zz()

extern itc_mbox_id_t itc_locate_sync_zz(const char *name, uint32_t wheretofind);
#define itc_locate_sync(name, wheretofind) itc_locate_sync_zz((name), (wheretofind))

extern int itc_get_fd_zz(void);
#define itc_get_fd() itc_get_fd_zz()

extern int32_t itc_get_name_zz(itc_mbox_id_t mbox_id, char *name, uint32_t name_len);
#define itc_get_name(mbox_id, name, name_len) itc_get_name_zz((mbox_id), (name), (name_len))
/*****************************************************************************\/
*****                    MAP TO BACKEND IMPLEMENTATION                     *****
*******************************************************************************/


#ifdef __cplusplus
}
#endif

#endif // __ITC_H__