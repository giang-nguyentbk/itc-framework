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
typedef int (itci_send)(struct itc_mailbox  *mbox,   
                        struct itc_message  *message,
                        itc_mbox_id_t       to,
                        itc_mbox_id_t       from);

typedef struct itc_message *(itci_receive)( struct itc_mailbox  *mbox,
                                            const uint32_t      *filter,
                                            long                tmo,
                                            itc_mbox_id_t       from,
                                            bool                recursive_call);

// Let's create a simple interface at now, but will be re-designed in a more complete way later.
struct itci_transport_apis {
    itci_send               *itci_send;
    itci_receive            *itci_receive;
};


#ifdef __cplusplus
}
#endif

#endif // __ITCI_TRANS_H__