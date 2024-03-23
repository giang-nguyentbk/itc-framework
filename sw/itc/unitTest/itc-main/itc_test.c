#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "itc_impl.h"
#include "itc.h"

void test_function(void);

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags);

/* Expect main call:    ./itc_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------
[FAILED]:       <test_function>          Failed to function(),                   rc = 2048!
[SUCCESS]:      <test_function>          Calling function() successful           rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	
	
	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

	test_itc_init(10, ITC_MALLOC, NULL, 0);

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");


	return 0;
}

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags)
{
	if(itc_init(nr_mboxes, alloc_scheme, namespace, init_flags) == false)
	{
		printf("[FAILED]:\t<test_itc_init>\t\t Failed to itc_init()!\n");
		return;
	}

        printf("[SUCCESS]:\t<test_itc_init>\t\t Calling itc_init() successful!\n");
}

