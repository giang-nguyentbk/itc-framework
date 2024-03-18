#include <stdlib.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_alloc.h"

/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN MALLOC-ATOR                   *****
*******************************************************************************/
/* This is a global variable for this file only, used for malloc allocator
   to manage how large memory block will be allocated.
   Should be updated by itc_init() via going through all transport mechanisms and see what is the minimum message size.
   By default, it's 1024*1024 ~ 10MB size. No limit for local and socket, but for sysvmq it's limited by msgctl()
   (normally it's one page size 4KB).

   TODO: There is something wrong in systemization here. Why did we choose max_msgsize only once at itc_init()
   and limit msg size of local and socket trans functions as same as sysvmq???
   What we should do is that we will choose which trans functions should be used depending on message size.
*/
static int max_mallocsize = 0;



/*****************************************************************************\/
*****                   ALLOC INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void malloc_init(struct result_code* rc, union itc_scheme *scheme_params, int max_msgsize);
static void malloc_exit(struct result_code* rc);
static struct itc_message* malloc_alloc(struct result_code* rc, size_t size);
static void malloc_free(struct result_code* rc, struct itc_message** message);
static struct itc_alloc_info malloc_getinfo(struct result_code* rc);

struct itci_alloc_apis malloc_apis = {
        malloc_init,
        malloc_exit,
        malloc_alloc,
        malloc_free,
        malloc_getinfo
};



/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
static void malloc_init(struct result_code* rc, union itc_scheme *scheme_params, int max_msgsize)
{
        // Because malloc allocator does not need any special scheme_params, see struct itc_malloc_scheme.
        // So we will ignore scheme_params here, users can just input nullptr for this argument.
        (void)scheme_params;
	(void)rc;

	if(max_msgsize < 0)
	{
		rc->flags |= ITC_INVALID_ARGUMENTS;
                return;
	}

        max_mallocsize = max_msgsize;
}

static void malloc_exit(struct result_code* rc)
{
	// Do nothing. Because malloc allocator has only one max_mallocsize varible, nothing to be cleaned up.
	(void)rc;
}

static struct itc_message *malloc_alloc(struct result_code* rc, size_t size)
{
        struct itc_message *retmessage;

        if(size > (size_t)max_mallocsize)
        {
                // Requested itc message's length is too large or itc_init() hasn't been called yet.
                // Should implement tracing/logging later for debugging purposes.
		rc->flags |= ITC_INVALID_ARGUMENTS;
                return NULL;
        }

        retmessage = (struct itc_message *)malloc(size);
        if(retmessage == NULL)
        {
                // Logging malloc() failed to allocate memory needed.
		rc->flags |= ITC_OUT_OF_MEM;
                return NULL;
        }

        return retmessage;
}

static void malloc_free(struct result_code* rc, struct itc_message** message)
{
	if(message == NULL || *message == NULL)
	{
		rc->flags |= ITC_FREE_NULL_PTR;
		return;
	}

        free(*message);
	/* Using: malloc_free(struct result_code* rc, struct itc_message* message)

	   Idk why but even if I assign this message = NULL. but after malloc_free return, in the context of the caller,
	   message is not NULL anymore, it's still pointing to its old address.
	   This is interesting and I'll investigate further later */

	/* Using: malloc_free(struct result_code* rc, struct itc_message** message)
	
	   UPDATE: I got the answer. In C, there is nothing called pass-by-reference like in C++.
	   So, even if you pass a pointer to a function that actually means you're passing a copy of an address
	   which is hold by the pointer, to the stack memory. So what? If you even assign NULL to the pointer, it still
	   cannot change actual original value outside the function.
	   
	   So, what could be a solution? This is declaring pointer-to-pointer "itc_message** message" in function
	   prototype and passing address of the pointer "&message" instead of passing address that pointer holds :D */
	*message = NULL;
}

static struct itc_alloc_info malloc_getinfo(struct result_code* rc)
{
	(void)rc;
        struct itc_alloc_info info;

        info.scheme = ITC_MALLOC;
        info.info.malloc_info.max_msgsize = max_mallocsize - ITC_HEADER_SIZE - 1;
        // max_mallocsize is the length of itc_message, so need to return to users itc_msg's length. They're only aware
        // of itc_msg, not itc_message which is used for internal control purposes.
                                                                
        return info;
}