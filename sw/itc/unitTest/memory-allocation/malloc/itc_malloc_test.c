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


static struct itci_alloc_apis allocator;
extern struct itci_alloc_apis malloc_apis;

void test_malloc_init(int max_msgsize);
void test_malloc_exit(void);
struct itc_message* test_malloc_alloc(size_t size);
void test_malloc_free(struct itc_message** message);
void test_malloc_getinfo(void);


/* Expect main call:    ./itc_malloc_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------
[FAILED]:       <test_malloc_init>               Failed to malloc_init(),                        rc = 2048!
[SUCCESS]:      <test_malloc_init>               Calling malloc_init() successful                rc = 0!
[FAILED]:       <test_malloc_alloc>              Failed to malloc_alloc(),                       rc = 2048!
[SUCCESS]:      <test_malloc_alloc>              Calling malloc_alloc() successful               rc = 0!
[SUCCESS]:      <test_malloc_free>               Calling malloc_free() successful                rc = 0!
[FAILED]:       <test_malloc_free>               Failed to malloc_free(),                        rc = 1024!
[SUCCESS]:      <test_malloc_getinfo>            Calling malloc_getinfo() successful             rc = 0!
[SUCCESS]:      <test_malloc_exit>               Calling malloc_exit() successful                rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables
	allocator = malloc_apis;
	struct itc_message* message;

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

	// Test malloc_init invalid max_msgsize 					ITC_INVALID_ARGUMENTS
        test_malloc_init(-1024);
        // Test malloc_init valid max_msgsize 						ITC_OK
        test_malloc_init(1024);
	// Test malloc_alloc with too large msgsize					ITC_INVALID_ARGUMENTS
        message = test_malloc_alloc((size_t)1500);


        // Test malloc_alloc with valid msgsize						ITC_OK
        message = test_malloc_alloc((size_t)100);
	// Test malloc_free successfully						ITC_OK
	test_malloc_free(&message);
	// Test malloc_free free nullptr, or double free due to previous call		ITC_FREE_NULL_PTR
	test_malloc_free(&message);


        // Test malloc_getinfo								ITC_OK
        test_malloc_getinfo();
	// Test malloc_exit								ITC_OK
        test_malloc_exit();

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

        return 0;
}





void test_malloc_init(int max_msgsize)
{
        struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_malloc_init>\t\t Failed to allocate result_code!\n");
                return;
	}

        if(allocator.itci_alloc_init != NULL)
        {
                allocator.itci_alloc_init(rc, max_msgsize);
                if(rc->flags != ITC_OK)
                {
                        printf("[FAILED]:\t<test_malloc_init>\t\t Failed to malloc_init(),\t\t\t rc = %d!\n", \
				rc->flags);
			free(rc);
                        return;
                }
        } else
        {
                printf("[FAILED]:\t<test_malloc_init>\t\t itci_alloc_init = NULL!\n");
		free(rc);
                return;
        }

        printf("[SUCCESS]:\t<test_malloc_init>\t\t Calling malloc_init() successful\t\t rc = %d!\n", rc->flags);
	free(rc);
        return;
}

void test_malloc_exit()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_malloc_exit>\t\t Failed to allocate result_code!\n");
                return;
	}

        if(allocator.itci_alloc_exit != NULL)
        {
                allocator.itci_alloc_exit(rc);
                if(rc->flags != ITC_OK)
                {
                        printf("[FAILED]:\t<test_malloc_exit>\t\t Failed to malloc_exit(),\t\t rc = %d!\n", \
				rc->flags);
			free(rc);
                        return;
                }
        } else
        {
                printf("[FAILED]:\t<test_malloc_exit>\t\t itci_alloc_exit = NULL!\n");
		free(rc);
                return;
        }

        printf("[SUCCESS]:\t<test_malloc_exit>\t\t Calling malloc_exit() successful\t\t rc = %d!\n", rc->flags);
	free(rc);
        return;
}

struct itc_message* test_malloc_alloc(size_t size)
{
	struct itc_message *message;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_malloc_alloc>\t\t Failed to allocate result_code!\n");
                return NULL;
	}
        
        if(allocator.itci_alloc_alloc != NULL)
        {
                message = allocator.itci_alloc_alloc(rc, size + ITC_HEADER_SIZE + 1); // Extra 1 byte is for ENDPOINT
                if(rc->flags != ITC_OK)
                {
                        printf("[FAILED]:\t<test_malloc_alloc>\t\t Failed to malloc_alloc(),\t\t\t rc = %d!\n", \
				rc->flags);
			free(rc);
                        return NULL;
                }
        } else
        {
                printf("[FAILED]:\t<test_malloc_alloc>\t\t itci_alloc_alloc = NULL!\n");
		free(rc);
                return NULL;
        }

        printf("[SUCCESS]:\t<test_malloc_alloc>\t\t Calling malloc_alloc() successful\t\t rc = %d!\n", rc->flags);
	free(rc);
        return message;
}

void test_malloc_free(struct itc_message** message)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_malloc_free>\t\t Failed to allocate result_code!\n");
                return;
	}

        if(allocator.itci_alloc_free != NULL)
        {
                allocator.itci_alloc_free(rc, message);
		if(rc->flags != ITC_OK)
                {
                        printf("[FAILED]:\t<test_malloc_free>\t\t Failed to malloc_free(),\t\t\t rc = %d!\n", \
				rc->flags);
			free(rc);
                        return;
                }
        } else
        {
                printf("[FAILED]:\t<test_malloc_free>\t\t itci_alloc_free = NULL!\n");
		free(rc);
                return;
        }

        printf("[SUCCESS]:\t<test_malloc_free>\t\t Calling malloc_free() successful\t\t rc = %d!\n", rc->flags);
	free(rc);
}

void test_malloc_getinfo()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_malloc_getinfo>\t\t Failed to allocate result_code!\n");
                return;
	}

        if(allocator.itci_alloc_getinfo != NULL)
        {
                allocator.itci_alloc_getinfo(rc);
		if(rc->flags != ITC_OK)
                {
                        printf("[FAILED]:\t<test_malloc_getinfo>\t\t Failed to malloc_getinfo(),\t\t rc = %d!\n", \
				rc->flags);
			free(rc);
                        return;
                }
        } else
        {
                printf("[FAILED]:\t<test_malloc_getinfo>\t\t itci_alloc_getinfo = NULL!\n");
		free(rc);
                return;
        }
        
        printf("[SUCCESS]:\t<test_malloc_getinfo>\t\t Calling malloc_getinfo() successful\t\t rc = %d!\n", rc->flags);
	free(rc);
        return;
}