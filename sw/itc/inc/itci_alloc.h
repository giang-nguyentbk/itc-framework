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


#define ENDPOINT (char)0xAA

typedef int                     (itci_init_alloc)(union itc_scheme *scheme_params,
                                                  int max_msgsize);
typedef int                     (itci_exit_alloc)(void);
typedef struct itc_message     *(itci_alloc)(size_t size); // This is sizeof(UserItcMessage), union itc_msg
typedef void                    (itci_free)(struct itc_message *message);

struct itci_alloc_apis {
        itci_init_alloc *itci_init_alloc;
        itci_exit_alloc *itci_exit_alloc;
        itci_alloc      *itci_alloc;
        itci_free       *itci_free;
};



#ifdef __cplusplus
}
#endif

#endif // __ITCI_ALLOC_H__