#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "itc_impl.h"
#include "itc.h"
#include "itc_threadmanager.h"
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


struct worker_t {
	void*			(*worker)(void*);
	pthread_key_t		destructor_key;
	pthread_mutex_t		mtx;
	int 			isTerminated;
};
static struct worker_t worker_1;
static struct worker_t worker_2;
static union itc_msg* msg_1 = NULL;
static union itc_msg* msg_2 = NULL;
static union itc_msg* rcv_msg = NULL;
static itc_mbox_id_t mbox_id_sending1_thread = ITC_NO_MBOX_ID;
static itc_mbox_id_t mbox_id_sending2_thread = ITC_NO_MBOX_ID;
static itc_mbox_id_t mbox_id_receiving_thread = ITC_NO_MBOX_ID;

static void receiving_thread_destructor();
static void* receiving_thread(void* data);
static void* sending2_thread(void* data);

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, uint32_t init_flags);
void test_itc_exit(void);
union itc_msg* test_itc_alloc(size_t size, uint32_t msgno);
void test_itc_free(union itc_msg **msg);
itc_mbox_id_t test_itc_create_mailbox(const char *name, uint32_t flags);
void test_itc_delete_mailbox(itc_mbox_id_t mbox_id);
void test_itc_send(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace);
union itc_msg *test_itc_receive(int32_t tmo);

itc_mbox_id_t test_itc_sender(union itc_msg *msg);
itc_mbox_id_t test_itc_receiver(union itc_msg *msg);
size_t test_itc_size(union itc_msg *msg);
itc_mbox_id_t test_itc_current_mbox(void);
int test_itc_get_fd();
void test_itc_get_name(itc_mbox_id_t mbox_id, char *name);
itc_mbox_id_t test_itc_locate_sync(int32_t timeout, const char *name, bool find_only_internal, bool *is_external, char *namespace);

/* Expect main call:    ./itc_test_2 */
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
	
	struct timespec t_start;
	struct timespec t_end;

	pthread_t receiving_thread_id;
	pthread_t sending2_thread_id;
	
	pthread_mutex_init(&worker_1.mtx, NULL);
	pthread_mutex_init(&worker_2.mtx, NULL);
	pthread_key_create(&worker_1.destructor_key, receiving_thread_destructor);
	
	PRINT_DASH_END;

	test_itc_init(10, ITC_MALLOC, 0);

	msg_1 = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzSetup1ReqS), MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ);


	mbox_id_sending1_thread = test_itc_create_mailbox("sending1_thread_mailbox", 0);
	printf("\tDEBUG: main - Starting sending1_thread with mailbox id = 0x%08x\n", mbox_id_sending1_thread);

	pthread_create(&sending2_thread_id, NULL, sending2_thread, NULL);

	sleep(1); // Make sure sending2_thread is ready
	printf("\tDEBUG: main - Started sending2_thread successfully!\n");
	MUTEX_LOCK(&worker_1.mtx);
	pthread_create(&receiving_thread_id, NULL, receiving_thread, NULL);
	MUTEX_LOCK(&worker_1.mtx);
	MUTEX_UNLOCK(&worker_1.mtx);


	(void)test_itc_current_mbox();
	(void)test_itc_get_fd();
	char name[255];
	test_itc_get_name(mbox_id_sending1_thread, name);
	itc_mbox_id_t mbox_id_receiving_thread_located = test_itc_locate_sync(1000, "teamServerMailbox1", 1, NULL, NULL);

	clock_gettime(CLOCK_MONOTONIC, &t_start);
	test_itc_send(&msg_1, mbox_id_receiving_thread_located, ITC_MY_MBOX_ID, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	unsigned long int difftime = calc_time_diff(t_start, t_end);
	printf("\tDEBUG: main - Time needed to send smg_1 = %lu (ns) -> %lu (ms)!\n", difftime, difftime/1000000);


	test_itc_delete_mailbox(mbox_id_sending1_thread);

	int ret = pthread_join(sending2_thread_id, NULL);
	if(ret != 0)
	{
		printf("\tDEBUG: main - ERROR pthread_join error code = %d\n", ret);
	}
	printf("\tDEBUG: main - Terminating sending2_thread...!\n");



	sleep(1); // Give teamServer_thread some time to finish receiving and handling the message
	ret = pthread_cancel(receiving_thread_id);
	if(ret != 0)
	{
		printf("\tDEBUG: main - ERROR pthread_cancel error code = %d\n", ret);
	}
	printf("\tDEBUG: main - Sent pthread_cancel, waiting for receiving_thread to finish its job...!\n");

	ret = pthread_join(receiving_thread_id, NULL);
	if(ret != 0)
	{
		printf("\tDEBUG: main - ERROR pthread_join error code = %d\n", ret);
	}
	printf("\tDEBUG: main - Terminated receiving_thread...!\n");


	(void)msg_1;
	// test_itc_free(&msg); // This will be freed by receiver "teamServer"

	test_itc_exit();

	PRINT_DASH_START;

	return 0;
}

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, uint32_t init_flags)
{
	if(itc_init(nr_mboxes, alloc_scheme, init_flags) == false)
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

union itc_msg* test_itc_alloc(size_t size, uint32_t msgno)
{
	union itc_msg* msg;

	msg = itc_alloc(size, msgno);

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

itc_mbox_id_t test_itc_create_mailbox(const char *name, uint32_t flags)
{
	itc_mbox_id_t mbox_id = itc_create_mailbox(name, flags);
	if(mbox_id == ITC_NO_MBOX_ID)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_create_mailbox>\t Failed to itc_create_mailbox()!\n");
		PRINT_DASH_END;
		return ITC_NO_MBOX_ID;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_create_mailbox>\t Calling itc_create_mailbox() successful!\n");
	PRINT_DASH_END;
	return mbox_id;
}

void test_itc_delete_mailbox(itc_mbox_id_t mbox_id)
{
	if(itc_delete_mailbox(mbox_id) == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_delete_mailbox>\t Failed to itc_delete_mailbox()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_delete_mailbox>\t Calling itc_delete_mailbox() successful!\n");
	PRINT_DASH_END;
}

void test_itc_send(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace)
{
	(void)namespace;
	if(itc_send(msg, to, from, NULL) == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_send>\t\t Failed to itc_send()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_send>\t\t Calling itc_send() successful!\n");
	PRINT_DASH_END;
}

union itc_msg *test_itc_receive(int32_t tmo)
{
	union itc_msg* msg;

	msg = itc_receive(tmo);

	if(msg == NULL)
	{
		if(tmo == ITC_NO_WAIT)
		{
			return NULL;
		} else
		{
			PRINT_DASH_START;
			printf("[FAILED]:\t<test_itc_receive>\t Failed to itc_receive()!\n");
			PRINT_DASH_END;
			return NULL;
		}
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_receive>\t Calling itc_receive() successful!\n");
	PRINT_DASH_END;
	return msg;
}

itc_mbox_id_t test_itc_sender(union itc_msg *msg)
{
	itc_mbox_id_t ret;

	ret = itc_sender(msg);

	if(ret == ITC_NO_MBOX_ID)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_sender>\t Failed to itc_sender()!\n");
		PRINT_DASH_END;
		return ITC_NO_MBOX_ID;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_sender>\t Calling itc_sender() successful!\n");
	PRINT_DASH_END;
	return ret;
}

itc_mbox_id_t test_itc_receiver(union itc_msg *msg)
{
	itc_mbox_id_t ret;

	ret = itc_receiver(msg);

	if(ret == ITC_NO_MBOX_ID)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_receiver>\t Failed to itc_receiver()!\n");
		PRINT_DASH_END;
		return ITC_NO_MBOX_ID;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_receiver>\t Calling itc_receiver() successful!\n");
	PRINT_DASH_END;
	return ret;
}

size_t test_itc_size(union itc_msg *msg)
{
	size_t ret;

	ret = itc_size(msg);

	if(ret == 0)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_size>\t Failed to itc_size()!\n");
		PRINT_DASH_END;
		return 0;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_size>\t Calling itc_size() successful!\n");
	PRINT_DASH_END;
	return ret;
}

itc_mbox_id_t test_itc_current_mbox()
{
	itc_mbox_id_t ret;

	ret = itc_current_mbox();

	if(ret == ITC_NO_MBOX_ID)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_current_mbox>\t Failed to itc_current_mbox()!\n");
		PRINT_DASH_END;
		return ITC_NO_MBOX_ID;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_current_mbox>\t Calling itc_current_mbox() successful, my mailbox id = 0x%08x\n", ret);
	PRINT_DASH_END;
	return ret;
}

int test_itc_get_fd()
{
	int mbox_fd = 0;

	mbox_fd = itc_get_fd();
	if(mbox_fd == -1)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_get_fd>\t\t Failed to itc_get_fd()!\n");
		PRINT_DASH_END;
		return -1;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_get_fd>\t\t Calling itc_get_fd() successful, mbox_fd = %d!\n", mbox_fd);
	PRINT_DASH_END;
	return mbox_fd;
}

void test_itc_get_name(itc_mbox_id_t mbox_id, char *name)
{
	if(itc_get_name(mbox_id, name) == false)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_get_name>\t\t Failed to itc_get_name()!\n");
		PRINT_DASH_END;
		return;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_get_name>\t\t Calling itc_get_name() successful, mbox name = %s!\n", name);
	PRINT_DASH_END;
}

itc_mbox_id_t test_itc_locate_sync(int32_t timeout, const char *name, bool find_only_internal, bool *is_external, char *namespace)
{
	itc_mbox_id_t ret;

	ret = itc_locate_sync(timeout, name, find_only_internal, is_external, namespace);

	if(ret == ITC_NO_MBOX_ID)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_itc_locate_sync>\t Failed to itc_locate_sync()!\n");
		PRINT_DASH_END;
		return ITC_NO_MBOX_ID;
	}

	PRINT_DASH_START;
        printf("[SUCCESS]:\t<test_itc_locate_sync>\t Calling itc_locate_sync() successful, my mailbox id = 0x%08x\n", ret);
	PRINT_DASH_END;
	return ret;
}





static void* receiving_thread(void* data)
{
	(void)data;
	
	printf("\tDEBUG: receiving_thread - Starting Receiving Thread...\n");

	uint32_t* destruct_data = (uint32_t*)malloc(sizeof(uint32_t));

	mbox_id_receiving_thread = test_itc_create_mailbox("teamServerMailbox1", 0);
	printf("\tDEBUG: receiving_thread - Starting Receiving Thread with mailbox id = 0x%08x\n", mbox_id_receiving_thread);

	*destruct_data = mbox_id_receiving_thread;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL); // Allow this thread can be cancelled without having any cancellation point such as sleep(), read(),...
	pthread_setspecific(worker_1.destructor_key, destruct_data);

	MUTEX_UNLOCK(&worker_1.mtx);
	printf("\tDEBUG: receiving_thread - UNLOCK for sending1_thread...\n");
	MUTEX_UNLOCK(&worker_2.mtx);
	printf("\tDEBUG: receiving_thread - UNLOCK for sending2_thread...\n");

	while(!worker_1.isTerminated)
	{
		// teamServerMailbox1 always listens to resourceHandlerMailbox1
		// printf("\tDEBUG: teamServerThread - Reading rx queue...!\n"); SPAM
		// Because ITC_FROM_ALL is not implemented yet, so must use ITC_NO_WAIT here to check if messages from two sending threads 
		rcv_msg = test_itc_receive(ITC_NO_WAIT);

		if(rcv_msg != NULL)
		{
			switch (rcv_msg->msgNo)
			{
			case MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ:
				{
					printf("\tDEBUG: receiving_thread - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					break;
				}

			case MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ:
				{
					printf("\tDEBUG: receiving_thread - Received MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					break;
				}

			default:
				{
					printf("\tDEBUG: receiving_thread - Received unknown message msgno = %u, sender = 0x%08x, discard it!\n", rcv_msg->msgNo, test_itc_sender(rcv_msg));
					test_itc_free(&rcv_msg);
					break;
				}
			}
		}
	}

	return NULL;
}

static void receiving_thread_destructor(void* data)
{
	// printf("\tDEBUG: Calling thread_destructor!\n");
	worker_1.isTerminated = 1;
	free(data);

	printf("\tDEBUG: receiving_thread_destructor - rcv_msg = 0x%08lx\n", (unsigned long)rcv_msg);

	test_itc_delete_mailbox(mbox_id_receiving_thread);

	if(rcv_msg != NULL)
	{
		test_itc_free(&rcv_msg);
	}
}

static void* sending2_thread(void* data)
{
	(void)data;
	
	struct timespec t_start;
	struct timespec t_end;

	mbox_id_sending2_thread = test_itc_create_mailbox("sending2_Thread", 0);
	printf("\tDEBUG: sending2_thread - Starting sending2_thread with mailbox id = 0x%08x\n", mbox_id_sending2_thread);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL); // Allow this thread can be cancelled without having any cancellation point such as sleep(), read(),...

	msg_2 = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzActivateReqS), MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ);


	MUTEX_LOCK(&worker_2.mtx);
	printf("\tDEBUG: sending2_thread - Pausing here...\n");
	MUTEX_LOCK(&worker_2.mtx); // Will stop here until teamServerThread MUTEX_UNLOCK &worker2.mtx
	MUTEX_UNLOCK(&worker_2.mtx);
	printf("\tDEBUG: sending2_thread - Resuming sending msg_2...\n");

	clock_gettime(CLOCK_MONOTONIC, &t_start);
	test_itc_send(&msg_2, mbox_id_receiving_thread, ITC_MY_MBOX_ID, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	unsigned long int difftime = calc_time_diff(t_start, t_end);
	printf("\tDEBUG: sending2_thread - Time needed to send msg_2 = %lu (ns) -> %lu (ms)!\n", difftime, difftime/1000000);

	test_itc_delete_mailbox(mbox_id_sending2_thread);

	if(msg_2 != NULL)
	{
		test_itc_free(&msg_2);
	}

	return NULL;
}

