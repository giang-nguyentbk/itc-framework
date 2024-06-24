#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

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



static volatile bool isTerminated = false;

void interrupt_handler(int dummy) {
	(void)dummy;
	isTerminated = true;
}

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

/* Expect main call:    ./itc_test_receiver */
int main(int argc, char* argv[])
{
	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables

	signal(SIGINT, interrupt_handler);
	
	PRINT_DASH_END;

	test_itc_init(10, ITC_MALLOC, 0);

	union itc_msg* send_msg;

	itc_mbox_id_t receiver_mbox_id = test_itc_create_mailbox("receiverMailbox", 0);

	union itc_msg* rcv_msg;
	// itc_mbox_id_t sender_mbox_id = 0x00300001;
	itc_mbox_id_t sender_mbox_id = ITC_NO_MBOX_ID;
	bool is_external = false;
	char namespace[255];
	int numOfCycles = 7;
	while(!isTerminated)
	{
		rcv_msg = test_itc_receive(ITC_WAIT_FOREVER);

		if(rcv_msg != NULL)
		{
			if(sender_mbox_id == ITC_NO_MBOX_ID)
			{
				sender_mbox_id = test_itc_locate_sync(5000, "senderMailbox", 0, &is_external, namespace);
				if(receiver_mbox_id != ITC_NO_MBOX_ID)
				{
					
				} else
				{
					printf("\tDEBUG: sender - Failed to locate sender mailbox!");
					return -1;
				}
			}

			switch (rcv_msg->msgNo)
			{
			case MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ:
				{
					printf("\tDEBUG: receiver - Received MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					send_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzSetup1CfmS), MODULE_XYZ_INTERFACE_ABC_SETUP1_CFM);
					if(send_msg != NULL)
					{
						send_msg->InterfaceAbcModuleXyzSetup1Cfm.clientId = 1;
						send_msg->InterfaceAbcModuleXyzSetup1Cfm.procedureId = 1;
						send_msg->InterfaceAbcModuleXyzSetup1Cfm.serverId = 1;
					} else
					{
						return -1;
					}
					if(is_external)
					{
						test_itc_send(&send_msg, receiver_mbox_id, ITC_MY_MBOX_ID, namespace);
					} else
					{
						test_itc_send(&send_msg, receiver_mbox_id, ITC_MY_MBOX_ID, NULL);
					}
					break;
				}

			case MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ:
				{
					printf("\tDEBUG: receiver - Received MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ, sender = 0x%08x, receiver = 0x%08x, payload length = %lu\n", \
						test_itc_sender(rcv_msg), test_itc_receiver(rcv_msg), test_itc_size(rcv_msg));
					test_itc_free(&rcv_msg);
					send_msg = test_itc_alloc(sizeof(struct InterfaceAbcModuleXyzActivateCfmS), MODULE_XYZ_INTERFACE_ABC_ACTIVATE_CFM);
					if(send_msg != NULL)
					{
						send_msg->InterfaceAbcModuleXyzActivateCfm.clientId = 1;
						send_msg->InterfaceAbcModuleXyzActivateCfm.procedureId = 1;
						send_msg->InterfaceAbcModuleXyzActivateCfm.serverId = 1;
					} else
					{
						return -1;
					}
					if(is_external)
					{
						test_itc_send(&send_msg, receiver_mbox_id, ITC_MY_MBOX_ID, namespace);
					} else
					{
						test_itc_send(&send_msg, receiver_mbox_id, ITC_MY_MBOX_ID, NULL);
					}
					printf("\tDEBUG: receiver - Activated device from receiver!\n");
					--numOfCycles;
					if(numOfCycles < 1)
					{
						isTerminated = true;
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


	test_itc_delete_mailbox(receiver_mbox_id);

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
		isTerminated = true;
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
	if(itc_send(msg, to, from, namespace) == false)
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