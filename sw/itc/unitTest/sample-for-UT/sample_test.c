#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "itc_impl.h"

void test_function(void);


/* Expect main call:    ./sample_test */
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

	test_function();

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");


	return 0;
}

void test_function()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t<test_q_init>\t\t Failed to allocate result_code!\n");
                return;
	}

	function(rc, ...);
	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t<test_q_init>\t\t Failed to q_init(),\t\t\t rc = %d!\n", \
			rc->flags);
		free(rc);
		return;
	}

        printf("[SUCCESS]:\t<test_q_init>\t\t Calling q_init() successful\t\t\t rc = %d!\n", rc->flags);
	free(rc);
}

