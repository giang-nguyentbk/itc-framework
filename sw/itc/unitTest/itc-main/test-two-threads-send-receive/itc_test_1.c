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
static itc_mbox_id_t mbox_id_sender = ITC_NO_MBOX_ID;
static itc_mbox_id_t mbox_id_receiver = ITC_NO_MBOX_ID;

static void exit_handler(void) {
	if(mbox_id_sender != ITC_NO_MBOX_ID)
	{
		itc_delete_mailbox(mbox_id_sender);
	}

	itc_exit();
	worker_1.isTerminated = 1;
}

static void teamServer_thread_destructor();
static void* teamServer_thread(void* data);

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

/* Expect main call:    ./itc_test_1 */
int main(int argc, char* argv[])
{

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	atexit(exit_handler);

	pthread_t teamServer_thread_id;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	
	pthread_mutex_init(&worker_1.mtx, NULL);
	pthread_key_create(&worker_1.destructor_key, teamServer_thread_destructor);
	
	PRINT_DASH_END;

	test_itc_init(10, ITC_MALLOC, 0);

	MUTEX_LOCK(&worker_1.mtx);
	pthread_create(&teamServer_thread_id, NULL, teamServer_thread, NULL);
	MUTEX_LOCK(&worker_1.mtx);
	MUTEX_UNLOCK(&worker_1.mtx);


	mbox_id_sender = test_itc_create_mailbox("senderMailbox", 0);

	union itc_msg* send_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzSetup1ReqS), MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ);
	if(mbox_id_receiver != ITC_NO_MBOX_ID)
	{
		test_itc_send(&send_msg, mbox_id_receiver, ITC_MY_MBOX_ID, NULL);
	}
	else
	{
		worker_1.isTerminated = 1;
	}

	int numOfCycles = 15;
	while(!worker_1.isTerminated)
	{
		// teamServerMailbox1 always listens to resourceHandlerMailbox1
		// printf("\tDEBUG: teamServerThread - Reading rx queue...!\n"); SPAM
		// Because ITC_FROM_ALL is not implemented yet, so must use ITC_NO_WAIT here to check if messages from two sending threads 
		send_msg = test_itc_receive(ITC_WAIT_FOREVER);

		if(send_msg != NULL)
		{
			switch (send_msg->msgNo)
			{
			case MODULE_XYZ_INTERFACE_ABC_SETUP1_CFM:
				{
					printf("\tDEBUG: sender - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_CFM, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(send_msg), test_itc_receiver(send_msg), test_itc_size(send_msg));
					test_itc_free(&send_msg);
					send_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzActivateReqS), MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ);
					test_itc_send(&send_msg, mbox_id_receiver, ITC_MY_MBOX_ID, NULL);
					break;
				}

			case MODULE_XYZ_INTERFACE_ABC_ACTIVATE_CFM:
				{
					printf("\tDEBUG: sender - Received MODULE_XYZ_INTERFACE_ABC_ACTIVATE_CFM, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(send_msg), test_itc_receiver(send_msg), test_itc_size(send_msg));
					test_itc_free(&send_msg);
					printf("\tDEBUG: sender - Connect Sequence Done!\n");
					--numOfCycles;
					if(numOfCycles < 1)
					{
						worker_1.isTerminated = 1;
					} else
					{
						send_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzActivateReqS), MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ);
						test_itc_send(&send_msg, mbox_id_receiver, ITC_MY_MBOX_ID, NULL);
					}
					break;
				}

			default:
				{
					printf("\tDEBUG: sender - Received unknown message msgno = %u, sender = 0x%08x, discard it!\n", send_msg->msgNo, test_itc_sender(send_msg));
					test_itc_free(&send_msg);
					break;
				}
			}
		}
	}

	test_itc_delete_mailbox(mbox_id_sender);

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





static void* teamServer_thread(void* data)
{
	(void)data;
	
	printf("\tDEBUG: teamServer_thread - Starting teamServerThread...\n");

	mbox_id_receiver = test_itc_create_mailbox("teamServerMailbox1", 0);
	itc_mbox_id_t mbox_id_ts = mbox_id_receiver;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL); // Allow this thread can be cancelled without having any cancellation point such as sleep(), read(),...
	pthread_setspecific(worker_1.destructor_key, &mbox_id_ts);

	MUTEX_UNLOCK(&worker_1.mtx);

	union itc_msg* rcv_msg;
	int numOfCycles = 15;
	while(!worker_1.isTerminated)
	{
		rcv_msg = test_itc_receive(ITC_WAIT_FOREVER);

		if(rcv_msg != NULL)
		{
			switch (rcv_msg->msgNo)
			{
			case MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ:
				{
					printf("\tDEBUG: receiver - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					rcv_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzSetup1CfmS), MODULE_XYZ_INTERFACE_ABC_SETUP1_CFM);
					test_itc_send(&rcv_msg, mbox_id_sender, ITC_MY_MBOX_ID, NULL);
					break;
				}

			case MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ:
				{
					printf("\tDEBUG: receiver - Received MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					rcv_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzActivateCfmS), MODULE_XYZ_INTERFACE_ABC_ACTIVATE_CFM);
					test_itc_send(&rcv_msg, mbox_id_sender, ITC_MY_MBOX_ID, NULL);
					printf("\tDEBUG: receiver - Activated device from receiver!\n");
					--numOfCycles;
					if(numOfCycles < 1)
					{
						worker_1.isTerminated = 1;
					}
					break;
				}

			default:
				{
					printf("\tDEBUG: receiver - Received unknown message msgno = %u, sender = 0x%08x, discard it!\n", rcv_msg->msgNo, test_itc_sender(rcv_msg));
					test_itc_free(&rcv_msg);
					break;
				}
			}
		}
	}
	return NULL;
}

static void teamServer_thread_destructor()
{
	// printf("\tDEBUG: Calling thread_destructor!\n");
	worker_1.isTerminated = 1;
}