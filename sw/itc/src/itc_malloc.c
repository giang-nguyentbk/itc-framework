#include <stdlib.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_alloc.h"

/*********************************************************************************************************************\/
*****                                 FUNCTION PROTOTYPES AND STATIC GLOBAL VARIABLES                              *****
***********************************************************************************************************************/
/* This is a global variable for this file only, used for malloc allocator
   to manage how large memory block will be allocated.
   Should be updated by itc_init() via going through all transport mechanisms and see what is the minimum message size.
   By default, it's 1024*1024 ~ 10MB size. No limit for local and socket, but for sysv it's limited by msgctl()
   (normally it's one page size 4KB).

   TODO: There is something wrong in systemization here. Why did we choose max_msgsize only once at itc_init()
   and limit msg size of local and socket trans functions as same as sysv???
   What we should do is that we will choose which trans functions should be used depending on message size.
*/
static int max_mallocsize;


static  int                     malloc_init(union itc_scheme *scheme_params, int max_msgsize);
static  int                     malloc_exit(void);
static  struct itc_message     *malloc_alloc(size_t size);
static  void                    malloc_free(struct itc_message *message);
static  struct itc_alloc_info  (malloc_getinfo)(void);

struct itci_alloc_apis malloc_apis = {
        malloc_init,
        malloc_exit,
        malloc_alloc,
        malloc_free,
        malloc_getinfo
};
/*********************************************************************************************************************\/
*****                                 FUNCTION PROTOTYPES AND STATIC GLOBAL VARIABLES                              *****
***********************************************************************************************************************/



/*********************************************************************************************************************\/
*****                                               FUNCTION DEFINITIONS                                           *****
***********************************************************************************************************************/
static int malloc_init(union itc_scheme *scheme_params,
                                int max_msgsize)
{
        // Because malloc allocator does not need any special scheme_params, see struct itc_malloc_scheme.
        // So we will ignore scheme_params here, users can just input nullptr for this argument.
        (void)scheme_params;

        max_mallocsize = max_msgsize;

        return ITC_ALLOC_RET_OK;
}

static int malloc_exit(void)
{
        // Do nothing. Because malloc allocator has only one max_mallocsize varible, nothing to be cleaned up.
        return ITC_ALLOC_RET_OK;
}

static struct itc_message *malloc_alloc(size_t size)
{
        struct itc_message *retmessage;

        if(max_mallocsize < 0 || size > (size_t)max_mallocsize)
        {
                // Requested itc message's length is too large or itc_init() hasn't been called yet.
                // Should implement tracing/logging later for debugging purposes.
                return NULL;
        }

        retmessage = (struct itc_message *)malloc(size);
        if(retmessage == NULL)
        {
                // Logging malloc() failed to allocate memory needed.
                return NULL;
        }

        return retmessage;
}

static void malloc_free(struct itc_message *message)
{
        free(message);
}

static  struct itc_alloc_info (malloc_getinfo)(void)
{
        struct itc_alloc_info info;

        info.scheme = ITC_MALLOC;
        info.info.malloc_info.max_msgsize = max_mallocsize - ITC_HEADER_SIZE - 1;
        // max_mallocsize is the length of itc_message, so need to return to users itc_msg's length. They're only aware
        // of itc_msg, not itc_message which is used for internal control purposes.
                                                                
        return info;
}
/*********************************************************************************************************************\/
*****                                               FUNCTION DEFINITIONS                                           *****
***********************************************************************************************************************/