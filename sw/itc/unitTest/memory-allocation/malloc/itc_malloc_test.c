/* This is some test cases for malloc allocator's functions
   Which functions will be tested:
        1. malloc_init
        2. malloc_exit
        3. malloc_alloc
        4. malloc_free
        5. malloc_getinfo
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "itci_alloc.h"
#include "itc.h"
#include "itc_impl.h"
#include "moduleXyz.sig"


static struct itci_alloc_apis allocator;
extern struct itci_alloc_apis malloc_apis;

int                     testMallocInitAlloc(itc_alloc_scheme alloc_scheme, union itc_scheme *scheme_params);
int                     testMallocExitAlloc();
struct itc_message     *testMallocAlloc(size_t size);
void                    testMallocFree(struct itc_message *message);
void                    testMallocGetInfo(const struct itc_alloc_info alloc_info, size_t itc_msgsize);


/* Expect main call:    ./itc_malloc_test       0 
                                                |
                                                ITC_MALLOC
*/
int main(int argc, char* argv[])
{
        char *p;
        itc_alloc_scheme m_alloc_scheme = ITC_MALLOC;

        if(argc < 2)
        {
                printf("[FAIL]:\t\t<itc_malloc_test>\t\tNot enough argurments!\n");
                return -1;
        } else
        {
                m_alloc_scheme = (itc_alloc_scheme)strtol(argv[1], &p, 10);
        }

        // Test malloc_init
        testMallocInitAlloc(m_alloc_scheme, NULL);
        // Test malloc_exit
        testMallocExitAlloc();
        // Test malloc_alloc
        struct itc_message *message = testMallocAlloc(sizeof(struct InterfaceAbcModuleXyzSetup1ReqS));
        // Test malloc_free
        testMallocFree(message);
        // Test malloc_getinfo
        struct itc_alloc_info alloc_info;
        alloc_info.scheme = m_alloc_scheme;
        testMallocGetInfo(alloc_info, 1025); // FAIL CASE
        testMallocGetInfo(alloc_info, 1000); // PASS CASE

        return 0;
}





int testMallocInitAlloc(itc_alloc_scheme alloc_scheme,
                        union itc_scheme *scheme_params)
{
        if(alloc_scheme == ITC_MALLOC)
        {
                allocator = malloc_apis;
        } else
        {
                printf("[FAIL]:\t\t<testMallocInitAlloc>\t\tUnexpected alloc_scheme argument!\n");
                return -1;
        }

        int max_msgsize = 1024; // this is itc_msg_size + ITC_HEADER_SIZE + 1, assigned to max_mallocsize in itc_malloc.c

        if(allocator.itci_alloc_init != NULL)
        {
                int ret = allocator.itci_alloc_init(scheme_params, max_msgsize);
                if(ret != ITC_RET_OK)
                {
                        printf("[FAIL]:\t\t<testMallocInitAlloc>\t\tFailed to itci_alloc_init()!\n");
                        return -1;
                }
        } else
        {
                printf("[FAIL]:\t\t<testMallocInitAlloc>\t\titci_alloc_init = NULL!\n");
                return -1;
        }

        printf("[SUCCESS]:\t<testMallocInitAlloc>\t\tSuccessful!\n");
        return 1;
}

int testMallocExitAlloc()
{
        if(allocator.itci_alloc_exit != NULL)
        {
                int ret = allocator.itci_alloc_exit();
                if(ret != ITC_RET_OK)
                {
                        printf("[FAIL]:\t\t<testMallocExitAlloc>\t\tFailed to itci_alloc_exit()!\n");
                        return -1;
                }
        } else
        {
                printf("[FAIL]:\t\t<testMallocExitAlloc>\t\titci_alloc_exit = NULL!\n");
                return -1;
        }

        printf("[SUCCESS]:\t<testMallocExitAlloc>\t\tSuccessful!\n");
        return 1;
}

struct itc_message *testMallocAlloc(size_t size)
{
        struct itc_message *message;

        if(allocator.itci_alloc_alloc != NULL)
        {
                message = allocator.itci_alloc_alloc(size + ITC_HEADER_SIZE + 1); // Extra 1 byte is for ENDPOINT
                if(message == NULL)
                {
                        printf("[FAIL]:\t\t<testMallocAlloc>\t\tFailed to itci_alloc_alloc()!\n");
                        return NULL;
                }
        } else
        {
                printf("[FAIL]:\t\t<testMallocAlloc>\t\titci_alloc_alloc = NULL!\n");
                return NULL;
        }

        printf("[SUCCESS]:\t<testMallocAlloc>\t\tSuccessful!\n");
        return message;
}

void testMallocFree(struct itc_message *message)
{
        if(allocator.itci_alloc_free != NULL)
        {
                allocator.itci_alloc_free(message);
        } else
        {
                printf("[FAIL]:\t\t<testMallocFree>\t\titci_alloc_free = NULL!\n");
                return;
        }

        printf("[SUCCESS]:\t<testMallocFree>\t\tSuccessful!\n");
}

void testMallocGetInfo(const struct itc_alloc_info alloc_info, size_t itc_msgsize)
{
        struct itc_alloc_info ret;
        ret.scheme = ITC_INVALID_SCHEME;

        if(allocator.itci_alloc_getinfo != NULL)
        {
                ret = allocator.itci_alloc_getinfo();
        } else
        {
                printf("[FAIL]:\t\t<testMallocGetInfo>\t\titci_alloc_getinfo = NULL!\n");
                return;
        }

        if(alloc_info.scheme != ret.scheme)
        {
                printf("[FAIL]:\t\t<testMallocGetInfo>\t\tAllocation scheme does not matched, " \
                "current allocator using scheme = %d!\n", ret.scheme);
                return;
        } else if (itc_msgsize > ret.info.malloc_info.max_msgsize)
        {
                printf("[FAIL]:\t\t<testMallocGetInfo>\t\tITC Msg size is too large, " \
                "itc_msgsize = %ld, max_msgsize = %ld!\n", itc_msgsize, ret.info.malloc_info.max_msgsize);
                return;
        }
        
        printf("[SUCCESS]:\t<testMallocGetInfo>\t\tSuccessful!\n");
        return;
}