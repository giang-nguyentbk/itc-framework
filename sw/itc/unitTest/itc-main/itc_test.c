#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "itc_impl.h"
#include "itc.h"
#include "moduleXyz.sig"

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


void test_function(void);

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags);
void test_itc_exit(void);
union itc_msg* test_itc_alloc(void);
void test_itc_free(union itc_msg **msg);

/* Expect main call:    ./itc_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------

        DEBUG: lsock_locate_coord - connect: No such file or directory
        DEBUG: itc_init_zz - q_init the first mailbox to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 0 to itc_inst.free_mbox_queue!
        DEBUG: q_enqueue - Queue now is empty, add the first node!
        DEBUG: itc_init_zz - q_enqueue mailbox 1 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 2 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 3 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 4 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 5 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 6 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 7 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 8 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 9 to itc_inst.free_mbox_queue!
        DEBUG: itc_init_zz - q_enqueue mailbox 10 to itc_inst.free_mbox_queue!
        DEBUG: sysvmq_maxmsgsize - Get max msg size successfully, max_msgsize = 8192!
        DEBUG: start_itcthreads - Starting a thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_init>          Calling itc_init() successful!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_alloc>         Calling itc_alloc() successful!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_free>          Calling itc_free() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: terminate_itcthreads - Terminating a thread!
        DEBUG: q_exit - Queue still has items, removing them!
        DEBUG: q_dequeue - Queue currently has 11 items!
        DEBUG: q_dequeue - Queue currently has 10 items!
        DEBUG: q_dequeue - Queue currently has 9 items!
        DEBUG: q_dequeue - Queue currently has 8 items!
        DEBUG: q_dequeue - Queue currently has 7 items!
        DEBUG: q_dequeue - Queue currently has 6 items!
        DEBUG: q_dequeue - Queue currently has 5 items!
        DEBUG: q_dequeue - Queue currently has 4 items!
        DEBUG: q_dequeue - Queue currently has 3 items!
        DEBUG: q_dequeue - Queue currently has 2 items!
        DEBUG: q_dequeue - Queue has only one item!
        DEBUG: q_dequeue - Queue empty!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_exit>          Calling itc_exit() successful!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	union itc_msg* msg;

	
	PRINT_DASH_END;

	test_itc_init(10, ITC_MALLOC, NULL, 0);

	msg = test_itc_alloc();

	test_itc_free(&msg);

	test_itc_exit();

	PRINT_DASH_START;


	return 0;
}

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, char *namespace, uint32_t init_flags)
{
	if(itc_init(nr_mboxes, alloc_scheme, namespace, init_flags) == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_init>\t\t Failed to itc_init()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_init>\t\t Calling itc_init() successful!\n");
	PRINT_DASH_END;
}

void test_itc_exit()
{
	if(itc_exit() == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_exit>\t\t Failed to itc_exit()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_exit>\t\t Calling itc_exit() successful!\n");
	PRINT_DASH_END;
}

union itc_msg* test_itc_alloc()
{
	union itc_msg* msg;

	msg = itc_alloc(sizeof(struct InterfaceAbcModuleXyzSetup1ReqS), MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ);

	if(msg == NULL)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_alloc>\t Failed to itc_alloc()!\n");
		PRINT_DASH_END;
		return NULL;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_alloc>\t Calling itc_alloc() successful!\n");
	PRINT_DASH_END;
	return msg;
}

void test_itc_free(union itc_msg **msg)
{
	if(itc_free(msg) == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_free>\t\t Failed to itc_free()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_free>\t\t Calling itc_free() successful!\n");
	PRINT_DASH_END;
}