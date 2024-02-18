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



/*********************************************************************************************************************\/
*****                                               ITC MACRO DECLARATIONS                                         *****
***********************************************************************************************************************/
#define ITC_NAME_MAXLEN 255

// If you make sure your mailbox's names you set later will be unique across the entire universe, you can use this flag
// for itc_init() call
#define ITC_NO_NAMESPACE NULL


/*********************************************************************************************************************\/
*****                                               ITC TYPE DECLARATIONS                                          *****
***********************************************************************************************************************/
typedef uint32_t itc_mbox_id_t;


/*****************************************************************************\/
*****                        ALLOCATION MECHANISM                          *****
*******************************************************************************/
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
*****                        ALLOCATION MECHANISM                          *****
*******************************************************************************/



/*********************************************************************************************************************\/
*****                                               ITC API DECLARATIONS                                           *****
***********************************************************************************************************************/
/*
*  Initialize important infrastructure for ITC system (Only once per a process).
*  1. Need to be called once in a process before any other ITC call.
*  2. Specify how many mailboxes ("number_of_mailboxes") and pre-allocate them continuously in a block
*  (aligned with 16-byte, same as malloc work).
*  3. When user call itc_create_mailbox(), take an mailbox available from a block and give it to user.
*/
extern int itc_init(int32_t number_of_mailboxes,
                    itc_alloc_scheme alloc_scheme,
                    union itc_scheme *scheme_params,
                    char *namespace, // Will be implemented later
                    uint32_t init_flags); // First usage is to see if itc_coor or not,
                                            // this is reserved for future usages.





#ifdef __cplusplus
}
#endif

#endif // __ITC_H__