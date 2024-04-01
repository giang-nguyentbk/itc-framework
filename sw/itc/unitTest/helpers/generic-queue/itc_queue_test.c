#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "itc_queue.h"
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


struct itc_queue* test_q_init(void);
void test_q_exit(struct itc_queue* q);
void test_q_enqueue(struct itc_queue*q, void* data);
void test_q_dequeue_int(struct itc_queue*q, int data_to_compare);
void test_q_dequeue_float(struct itc_queue*q, float data_to_compare);
void test_q_remove(struct itc_queue* q, void* data);
void test_q_clear(struct itc_queue* q);

/* Expect main call:    ./itc_queue_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_init>            Calling q_init() successful,                            rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_enqueue - Queue now is empty, add the first node!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_enqueue>         Calling q_enqueue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_enqueue>         Calling q_enqueue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_enqueue>         Calling q_enqueue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_enqueue>         Calling q_enqueue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_dequeue - Queue currently has 4 items!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_dequeue_int>     Calling q_dequeue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_dequeue - Queue currently has 3 items!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_dequeue_int>     Calling q_dequeue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_dequeue - Queue currently has 2 items!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_dequeue_float>   Calling q_dequeue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_dequeue - Queue has only one item!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_dequeue_float>   Calling q_dequeue() successful,                         rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_q_exit>            Calling q_exit() successful,                            rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	struct itc_queue* q;

	int* datac = (int*)malloc(2*sizeof(int));
	datac[0] = 1;
	datac[1] = 2;

	int* data = (int*)malloc(5*sizeof(int));
	data[0] = 1;
	data[1] = 2;
	
	data[2] = 3;
	data[3] = 4;
	data[4] = 5;

	float* fdata = (float*)malloc(2*sizeof(float));
	fdata[0] = 1.5;
	fdata[1] = 2.5;

	PRINT_DASH_END;

	q = test_q_init(); 			// OK

	test_q_enqueue(q, datac + 0);		// OK		--> To be removed by q_clear
	test_q_enqueue(q, datac + 1);		// OK		--> To be removed by q_clear

	test_q_clear(q); 			// OK

	test_q_enqueue(q, data + 2);		// OK		--> To be removed by q_remove before q_dequeue
	test_q_enqueue(q, data + 1);		// OK
	test_q_enqueue(q, data + 3);		// OK		--> To be removed by q_remove before q_dequeue
	test_q_enqueue(q, data);		// OK
	test_q_enqueue(q, data + 4);		// OK		--> To be removed by q_remove before q_dequeue
	test_q_enqueue(q, fdata + 1);		// OK
	test_q_enqueue(q, fdata);		// OK

	test_q_remove(q, data + 2);		// OK
	test_q_remove(q, data + 3);		// OK
	test_q_remove(q, data + 4);		// OK

	test_q_dequeue_int(q, data[1]);		// OK
	test_q_dequeue_int(q, data[0]);		// OK
	test_q_dequeue_float(q, fdata[1]);	// OK
	test_q_dequeue_float(q, fdata[0]);	// OK

	test_q_exit(q);				// OK

	PRINT_DASH_START;

	free(datac);
	free(data);
	free(fdata);

	return 0;
}

struct itc_queue* test_q_init()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_init>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return NULL;
	}

	struct itc_queue* q;
	q = q_init(rc);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_init>\t\t Failed to q_init(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return NULL;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_init>\t\t Calling q_init() successful,\t\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);

	return q;
}

void test_q_exit(struct itc_queue* q)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_exit>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	q_exit(rc, q);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_exit>\t\t Failed to q_exit(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_exit>\t\t Calling q_exit() successful,\t\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_q_enqueue(struct itc_queue*q, void* data)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_enqueue>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	q_enqueue(rc, q, data);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_enqueue>\t Failed to q_enqueue(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_enqueue>\t Calling q_enqueue() successful,\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_q_dequeue_int(struct itc_queue*q, int data_to_compare)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_int>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	void* data;
	data = q_dequeue(rc, q);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_int>\t Failed to q_dequeue(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	if(*(int*)data != data_to_compare)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_int>\t Data after dequeueing not equal!,\t\t\t data = %d, data_to_compare = %d!\n", \
			*(int*)data, data_to_compare);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_dequeue_int>\t Calling q_dequeue() successful,\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_q_dequeue_float(struct itc_queue*q, float data_to_compare)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_float>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	void* data;
	data = q_dequeue(rc, q);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_float>\t Failed to q_dequeue(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	if(*(float*)data != data_to_compare)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_dequeue_float>\t Data after dequeueing not equal!,\t\t\t data = %f, data_to_compare = %f!\n", \
			*(float*)data, data_to_compare);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_dequeue_float>\t Calling q_dequeue() successful,\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_q_remove(struct itc_queue* q, void* data)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_remove>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	q_remove(rc, q, data);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_remove>\t Failed to q_remove(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_remove>\t Calling q_remove() successful,\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_q_clear(struct itc_queue* q)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_clear>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	q_clear(rc, q);
	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_q_clear>\t Failed to q_clear(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_q_clear>\t Calling q_clear() successful,\t\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}