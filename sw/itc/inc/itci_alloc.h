/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

/*  I think that we first need to create a mechanism on how to allocate memory for itc message.
    Same as trans mechanism interface, we will create an interface for allocation functions.
    We will offer users (alloc_scheme decided at initialization of ITC, itc_init()) some different ways
    to allocate new itc messages:
        1. Generalised Malloc: to allocate any size (bytes) of itc messages.
        2. Memory Pool: to pre-allocate memory blocks with fixed sizes
        such as 32, 224, 992, 4064, 16352, 65504 bytes. [OPTIONAL] // Will be implemented later
        // Same as Paging which is done by OS, if you keep allocating memory in heap via malloc ordinarily,
        your heap will be quickly fragmented. Instead of that, using fixed size of memory blocks, also called pools,
        which will help you efficiently ultilize heap memories.
*/

#ifndef __ITCI_ALLOC_H__
#define __ITCI_ALLOC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "itc.h"
#include "itc_impl.h"

typedef void (itci_alloc_init)(struct result_code* rc, int max_msgsize);
typedef void (itci_alloc_exit)(struct result_code* rc);
typedef struct itc_message* (itci_alloc_alloc)(struct result_code* rc, size_t size);
typedef void (itci_alloc_free)(struct result_code* rc, struct itc_message** message);
typedef struct itc_alloc_info (itci_alloc_getinfo)(struct result_code* rc);

struct itci_alloc_apis {
        itci_alloc_init         *itci_alloc_init;       // API to setup internal configuration in 
                                                        // individual allocation mechanism at itc_init().

        itci_alloc_exit         *itci_alloc_exit;       // API to release above configuration.
        itci_alloc_alloc        *itci_alloc_alloc;      // API to specify how we will allocate memory for itc messages.
        itci_alloc_free         *itci_alloc_free;       // API to deallocate itc messages.
        itci_alloc_getinfo      *itci_alloc_getinfo;    // API to get information about the current allocator.        
};



#ifdef __cplusplus
}
#endif

#endif // __ITCI_ALLOC_H__