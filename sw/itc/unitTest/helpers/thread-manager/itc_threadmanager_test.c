#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "itc_threadmanager.h"
#include "itc_impl.h"

#define PRINT_DASH_START					\
	do							\
	{							\
		printf("\n-------------------------------------------------------------------------------------------------------------------\n");	\
	} while(0)

#define PRINT_DASH_END						\
	do							\
	{							\
		printf("-------------------------------------------------------------------------------------------------------------------\n\n");	\
	} while(0)


struct worker_t {
	void*			(*worker)(void*);
	pthread_mutex_t		start_mtx;
	pthread_key_t		destructor_key;
	int*			data;
	int 			isTerminated;
};

static void thread_destructor();
static void* worker_function_1(void* data);
static struct worker_t worker_1;

void test_set_sched_params(int policy, int selflimit_prio, int priority);
void test_add_itcthread(void* (*worker)(void*), void* arg, bool use_highest_prio, pthread_mutex_t* start_mtx);
void test_start_itcthreads(void);
void test_terminate_itcthreads(void);

/* Expect main call:    ./itc_threadmanager_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------

        DEBUG: check_sched_params - Invalid priority config, prio = 100, min_prio = 1, max_prio = 99, selflimit_prio = 40!
        DEBUG: set_sched_params - check_sched_params failed!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:       <test_set_sched_params>          Failed to set_sched_params(),                   rc = 1024!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_set_sched_params>          Calling set_sched_params() successful           rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_set_sched_params>          Calling set_sched_params() successful           rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: set_sched_params - Configure SCHED_OTHER for this thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_set_sched_params>          Calling set_sched_params() successful           rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_add_itcthread>             Calling add_itcthread() successful              rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: start_itcthreads - Starting a thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_start_itcthreads>          Calling start_itcthreads() successful           rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: terminate_itcthreads - Terminating a thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_terminate_itcthreads>      Calling terminate_itcthreads() successful       rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	worker_1.worker = worker_function_1;
	pthread_mutex_init(&worker_1.start_mtx, NULL);
	pthread_key_create(&worker_1.destructor_key, thread_destructor);
	
	PRINT_DASH_END;

	test_set_sched_params(SCHED_FIFO, ITC_HIGH_PRIORITY, 100); // NOK
	test_set_sched_params(SCHED_FIFO, ITC_HIGH_PRIORITY, 15); // OK
	test_set_sched_params(SCHED_RR, ITC_HIGH_PRIORITY, 15); // OK
	test_set_sched_params(SCHED_OTHER, ITC_HIGH_PRIORITY, 10); // OK

	test_add_itcthread(worker_1.worker, NULL, true, &worker_1.start_mtx); // OK

	test_start_itcthreads(); // OK
	test_terminate_itcthreads(); // OK

	PRINT_DASH_START;

	pthread_mutex_destroy(&worker_1.start_mtx);
	pthread_key_delete(worker_1.destructor_key);

	return 0;
}

void test_set_sched_params(int policy, int selflimit_prio, int priority)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_set_sched_params>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	set_sched_params(rc, policy, selflimit_prio, priority);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_set_sched_params>\t\t Failed to set_sched_params(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_set_sched_params>\t\t Calling set_sched_params() successful\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_add_itcthread(void* (*worker)(void*), void* arg, bool use_highest_prio, pthread_mutex_t* start_mtx)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_add_itcthread>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	add_itcthread(rc, worker, arg, use_highest_prio, start_mtx);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_add_itcthread>\t\t Failed to add_itcthread(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_add_itcthread>\t\t Calling add_itcthread() successful\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_start_itcthreads(void)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_start_itcthreads>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	start_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_start_itcthreads>\t\t Failed to start_itcthreads(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_start_itcthreads>\t\t Calling start_itcthreads() successful\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_terminate_itcthreads(void)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_terminate_itcthreads>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	terminate_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_terminate_itcthreads>\t Failed to terminate_itcthreads(),\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_terminate_itcthreads>\t Calling terminate_itcthreads() successful\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

static void* worker_function_1(void* data)
{
	(void)data;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);
	
	// printf("\tDEBUG: Starting worker_function_1...\n");

	worker_1.data = (int*)malloc(sizeof(int));
	pthread_setspecific(worker_1.destructor_key, worker_1.data);

	MUTEX_UNLOCK(&worker_1.start_mtx, __FILE__, __LINE__);

	while(1)
	{
		if(worker_1.isTerminated)
		{
			break;
		}

		/* This is a very interesting thing here. Idk why but if no expressions after this point, then pthread_cancel cannot trigger
		* thread-specific data (worker1.data) destructor.
		* So we will do sleep() temporarily over here at the moment.
		*
		* Note: Okay, the problem was solved via pthread_setcanceltype() to PTHREAD_CANCEL_ASYNCHRONOUS.
		* For more details: See https://stackoverflow.com/questions/7961029/how-can-i-kill-a-pthread-that-is-in-an-infinite-loop-from-outside-that-loop
		* "By default your thread can't be cancelled with pthread_cancel() without calling any functions that are cancellation points."
		*
		* So, there might be that sleep() are a cancellation point??? */

		// sleep(1);
	}

	return NULL;
}

static void thread_destructor()
{
	// printf("\tDEBUG: Calling thread_destructor!\n");
	worker_1.isTerminated = 1;
	free(worker_1.data);
}
