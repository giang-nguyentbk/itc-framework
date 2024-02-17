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


#define ITC_NAME_MAXLEN     255

typedef uint32_t itc_mbox_id_t;

// Because currently allocation scheme using malloc does not need any special parameters for allocation.
// We will reserve it for future usages.
struct itc_malloc_scheme {
        unsigned int reserved;
};

union itc_scheme {
        struct itc_malloc_scheme        malloc_scheme;
};

#ifdef __cplusplus
}
#endif

#endif // __ITC_H__