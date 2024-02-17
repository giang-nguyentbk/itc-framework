/* This is some test cases for malloc allocator's functions
   Which functions will be tested:
        1. malloc_init_alloc
        2. malloc_exit_alloc
        3. malloc_alloc
        4. malloc_free
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

int                     testMallocInitAlloc(itc_alloc_scheme alloc_scheme,
                                        union itc_scheme *scheme_params);
int                     testMallocExitAlloc();
struct itc_message      *testMallocAlloc(size_t size);
void                    testMallocFree(struct itc_message *message);


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
                printf("[FAIL]:\t<itc_malloc_test>\t\tNot enough argurments!\n");
                return -1;
        } else
        {
                m_alloc_scheme = (itc_alloc_scheme)strtol(argv[1], &p, 10);
        }

        // Test malloc_init_alloc
        testMallocInitAlloc(m_alloc_scheme, NULL);
        // Test malloc_exit_alloc
        testMallocExitAlloc();
        // Test malloc_alloc
        struct itc_message *message = testMallocAlloc(sizeof(struct InterfaceAbcModuleXyzSetup1ReqS));
        // Test malloc_free
        testMallocFree(message);

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
                printf("[FAIL]:\t<testMallocInitAlloc>\t\tUnexpected alloc_scheme argument!\n");
                return -1;
        }

        int max_msgsize = 1024;

        if(allocator.itci_init_alloc != NULL)
        {
                int ret = allocator.itci_init_alloc(scheme_params, max_msgsize);
                if(ret != ITC_ALLOC_RET_OK)
                {
                        printf("[FAIL]:\t<testMallocInitAlloc>\t\tFailed to itci_init_alloc()!\n");
                        return -1;
                }
        } else
        {
                printf("[FAIL]:\t<testMallocInitAlloc>\t\titci_init_alloc = NULL!\n");
                return -1;
        }

        printf("[SUCCESS]:\t<testMallocInitAlloc>\t\tSuccessful!\n");
        return 1;
}

int testMallocExitAlloc()
{
        if(allocator.itci_exit_alloc != NULL)
        {
                int ret = allocator.itci_exit_alloc();
                if(ret != ITC_ALLOC_RET_OK)
                {
                        printf("[FAIL]:\t<testMallocExitAlloc>\t\tFailed to itci_exit_alloc()!\n");
                        return -1;
                }
        } else
        {
                printf("[FAIL]:\t<testMallocExitAlloc>\t\titci_exit_alloc = NULL!\n");
                return -1;
        }

        printf("[SUCCESS]:\t<testMallocExitAlloc>\t\tSuccessful!\n");
        return 1;
}

struct itc_message *testMallocAlloc(size_t size)
{
        struct itc_message *message;

        if(allocator.itci_alloc != NULL)
        {
                message = allocator.itci_alloc(size + ITC_HEADER_SIZE + 1); // Extra 1 byte is for ENDPOINT
                if(message == NULL)
                {
                        printf("[FAIL]:\t<testMallocAlloc>\t\tFailed to itci_alloc()!\n");
                        return NULL;
                }
        } else
        {
                printf("[FAIL]:\t<testMallocAlloc>\t\titci_alloc = NULL!\n");
                return NULL;
        }

        printf("[SUCCESS]:\t<testMallocAlloc>\t\tSuccessful!\n");
        return message;
}

void testMallocFree(struct itc_message *message)
{
        if(allocator.itci_free != NULL)
        {
                allocator.itci_free(message);
        } else
        {
                printf("[FAIL]:\t<testMallocFree>\t\titci_free = NULL!\n");
                return;
        }

        printf("[SUCCESS]:\t<testMallocFree>\t\tSuccessful!\n");
}