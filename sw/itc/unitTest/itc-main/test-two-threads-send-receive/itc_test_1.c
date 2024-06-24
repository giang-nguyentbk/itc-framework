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
static union itc_msg* msg = NULL;
static union itc_msg* rcv_msg = NULL;

static void teamServer_thread_destructor();
static void* teamServer_thread(void* data);

void test_itc_init(int32_t nr_mboxes, itc_alloc_scheme alloc_scheme, uint32_t init_flags);
void test_itc_exit(void);
union itc_msg* test_itc_alloc(void);
void test_itc_free(union itc_msg **msg);
itc_mbox_id_t test_itc_create_mailbox(const char *name, uint32_t flags);
void test_itc_delete_mailbox(itc_mbox_id_t mbox_id);
void test_itc_send(union itc_msg **msg, itc_mbox_id_t to, itc_mbox_id_t from, char *namespace);
union itc_msg *test_itc_receive(int32_t tmo);

/* Expect main call:    ./itc_test_1 */
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
        DEBUG: q_dequeue - Queue currently has 11 items!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_create_mailbox_zz - Failed to send notification to itccoord regarding ADD mailbox id = 5242880!
        DEBUG: sysvmq_rx_thread - Starting sysvmq_rx_thread itc_rx_sysvmq_0x00500000...!
        DEBUG: sysvmq_maxmsgsize - Get max msg size successfully, max_msgsize = 8192!
        DEBUG: MUTEX_LOCK       0x0011b418,     ../../../src/helpers/itc_threadmanager.c:114,   time_elapsed = 14 (ms)!
        DEBUG: MUTEX_LOCK - t_start.tv_sec = 30136!
        DEBUG: MUTEX_LOCK - t_start.tv_nsec = 970643713!
        DEBUG: MUTEX_LOCK - t_end.tv_sec = 30136!
        DEBUG: MUTEX_LOCK - t_end.tv_nsec = 985402251!
        DEBUG: start_itcthreads - Starting a thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_init>          Calling itc_init() successful!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_alloc>         Calling itc_alloc() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: q_dequeue - Queue currently has 10 items!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_create_mailbox_zz - Failed to send notification to itccoord regarding ADD mailbox id = 5242881!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_create_mailbox>        Calling itc_create_mailbox() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: teamServer_thread - Starting teamServerThread...
        DEBUG: q_dequeue - Queue currently has 9 items!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_create_mailbox_zz - Failed to send notification to itccoord regarding ADD mailbox id = 5242882!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_create_mailbox>        Calling itc_create_mailbox() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: itc_receive_zz - Waiting for incoming messages...!
        DEBUG: enqueue_message - RX queue has only one item, add a new one!
        DEBUG: itc_send_zz - Sent successfully on trans_mechanism[0]!
        DEBUG: itc_send_zz - Notify receiver about sent messages!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_send>          Calling itc_send() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: main - Time needed to send message = 5855299 (ns) -> 5 (ms)!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_delete_mailbox_zz - Failed to send notification to itccoord regarding RMV mailbox id = 5242881!
        DEBUG: itc_delete_mailbox_zz - Deleted thread-local mailbox 5242881!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_delete_mailbox>        Calling itc_delete_mailbox() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: dequeue_message - RX queue has only one item, dequeue it!
        DEBUG: itc_receive_zz - Received a message on trans_mechanisms[0]!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_receive>       Calling itc_receive() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: teamServerThread - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ, handle it!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:      <test_itc_free>          Calling itc_free() successful!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: itc_receive_zz - Waiting for incoming messages...!
        DEBUG: teamServer_thread_destructor - rcv_msg = 0x00000000, msg = 0x00000000!
        DEBUG: mailbox_destructor_at_thread_exit - Thread-local mailbox destructor called by tid = 221858, mbox_id = 5242882!
        DEBUG: mailbox_destructor_at_thread_exit - Deleting mailbox with mbox_id = 5242882!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_delete_mailbox_zz - Failed to send notification to itccoord regarding RMV mailbox id = 5242882!
        DEBUG: itc_delete_mailbox_zz - Deleted thread-local mailbox 5242882!
        DEBUG: main - Terminating teamServer_thread...!
        DEBUG: mailbox_destructor_at_thread_exit - Thread-local mailbox destructor called by tid = 221857, mbox_id = 5242880!
        DEBUG: mailbox_destructor_at_thread_exit - Deleting mailbox with mbox_id = 5242880!
        DEBUG: find_mbox - Mailbox not belong to this process, mbox_id = 1048577!
        DEBUG: find_localmbx_data - Not belong to this process, mbox_id = 1048576, my_mbox_id_in_itccoord = 5242880!
        DEBUG: local_send - Not belong to this process, mbox_id = 1048577!
        DEBUG: get_sysvmq_cl - Add contact list!
        DEBUG: get_sysvmq_id - msgget: No such file or directory
        DEBUG: sysvmq_send - Receiver side not initialised message queue yet!
        DEBUG: itc_send_zz - Failed to send message by all transport mechanisms!
        DEBUG: itc_delete_mailbox_zz - Failed to send notification to itccoord regarding RMV mailbox id = 5242880!
        DEBUG: itc_delete_mailbox_zz - Deleted thread-local mailbox 5242880!
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

	union itc_msg* msg;
	pthread_t teamServer_thread_id;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	
	pthread_mutex_init(&worker_1.mtx, NULL);
	pthread_key_create(&worker_1.destructor_key, teamServer_thread_destructor);
	
	PRINT_DASH_END;

	test_itc_init(10, ITC_MALLOC, 0);

	msg = test_itc_alloc();

	itc_mbox_id_t mbox_id_1 = test_itc_create_mailbox("resourceHandlerMailbox1", 0);

	MUTEX_LOCK(&worker_1.mtx);
	pthread_create(&teamServer_thread_id, NULL, teamServer_thread, NULL);
	MUTEX_LOCK(&worker_1.mtx);
	MUTEX_UNLOCK(&worker_1.mtx);

	clock_gettime(CLOCK_REALTIME, &t_start);

	itc_mbox_id_t mbox_id_2 = 0x00500002; // 0x00500002 if enable itc_sysvmq, otherwise 0x00500001
	test_itc_send(&msg, mbox_id_2, ITC_MY_MBOX_ID, NULL);

	clock_gettime(CLOCK_REALTIME, &t_end);
	unsigned long int difftime = calc_time_diff(t_start, t_end);
	printf("\tDEBUG: main - Time needed to send message = %lu (ns) -> %lu (ms)!\n", difftime, difftime/1000000);

	test_itc_delete_mailbox(mbox_id_1);

	sleep(1); // Give teamServer_thread some time to finish receiving and handling the message
	int ret = pthread_cancel(teamServer_thread_id);
	if(ret != 0)
	{
		printf("\tDEBUG: main - ERROR pthread_cancel error code = %d\n", ret);
	}

	ret = pthread_join(teamServer_thread_id, NULL);
	if(ret != 0)
	{
		printf("\tDEBUG: main - ERROR pthread_join error code = %d\n", ret);
	}
	printf("\tDEBUG: main - Terminating teamServer_thread...!\n");

	(void)msg;
	// test_itc_free(&msg); // This will be freed by receiver "teamServer"

	test_itc_exit();

	PRINT_DASH_START;


	free(rc);

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


static void* teamServer_thread(void* data)
{
	(void)data;
	
	printf("\tDEBUG: teamServer_thread - Starting teamServerThread...\n");

	itc_mbox_id_t mbox_id_ts = test_itc_create_mailbox("teamServerMailbox1", 0);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL); // Allow this thread can be cancelled without having any cancellation point such as sleep(), read(),...
	pthread_setspecific(worker_1.destructor_key, &mbox_id_ts);

	MUTEX_UNLOCK(&worker_1.mtx);

	while(1)
	{
		if(worker_1.isTerminated)
		{
			break;
		}

		// teamServerMailbox1 always listens to resourceHandlerMailbox1
		// printf("\tDEBUG: teamServerThread - Reading rx queue...!\n"); SPAM
		// Let's test with 1000 ms waiting for responses, ITC_NO_WAIT and ITC_WAIT_FOREVER
		rcv_msg = test_itc_receive(ITC_WAIT_FOREVER);

		if(rcv_msg != NULL)
		{
			switch (rcv_msg->msgNo)
			{
			case MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ:
				{
					printf("\tDEBUG: teamServerThread - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ, handle it!\n");
					test_itc_free(&rcv_msg);
					break;
				}
			
			default:
				{
					printf("\tDEBUG: teamServerThread - Received unknown message msgno = %u, discard it!\n", rcv_msg->msgNo);
					test_itc_free(&rcv_msg);
					break;
				}
			}
		}
	}

	test_itc_delete_mailbox(mbox_id_ts);
	return NULL;
}

static void teamServer_thread_destructor()
{
	// printf("\tDEBUG: Calling thread_destructor!\n");
	worker_1.isTerminated = 1;

	printf("\tDEBUG: teamServer_thread_destructor - rcv_msg = 0x%08lx, msg = 0x%08lx!\n", (unsigned long)rcv_msg, (unsigned long)msg);
	if(rcv_msg != NULL)
	{
		test_itc_free(&rcv_msg);
	} else if(msg != NULL)
	{
		test_itc_free(&msg);
	}
}