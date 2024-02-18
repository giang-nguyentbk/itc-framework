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
*****                          VARIABLE MACROS                             *****
*******************************************************************************/
#define ENDPOINT (char)0xAA
#define ITC_HEADER_SIZE 14 // itc_message: flags + receiver + sender + size. Also is the offset between
                                // the starting of itc_message and the starting of itc_msg.
/*****************************************************************************\/
*****                          VARIABLE MACROS                             *****
*******************************************************************************/



/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/
// Flags to see if you're itc_coordinator or not (used by itc_init() call)
#define ITC_FLAGS_I_AM_ITC_COOR 0x00000001
/*****************************************************************************\/
*****                          FLAG DEFINITIONS                            *****
*******************************************************************************/



/*****************************************************************************\/
*****                            RETURN CODE                               *****
*******************************************************************************/
// Result code used for allocator error handling
#define ITC_ALLOC_RET_OK 0
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
/*****************************************************************************\/
*****                         TYPE DEFINITIONS                             *****
*******************************************************************************/


#ifdef __cplusplus
}
#endif

#endif // __ITC_IMPL_H__