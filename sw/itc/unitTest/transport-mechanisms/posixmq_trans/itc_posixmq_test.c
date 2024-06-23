#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "itci_trans.h"
#include "itc.h"
#include "itc_impl.h"
#include "itc_threadmanager.h"

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

static struct itci_transport_apis transporter;
extern struct itci_transport_apis posixmq_trans_apis;

void test_posixmq_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags);
void test_posixmq_exit(void);
void test_posixmq_send(struct itc_message *message, itc_mbox_id_t to);
struct itc_message *test_posixmq_receive(struct itc_mailbox *my_mbox);
int test_posixmq_maxmsgsize(void);


/* Expect main call:    ./itc_posixmq_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_posixmq_maxmsgsize>         posixmq_maxmsgsize() successfully = 8192 bytes,  rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: posixmq_send - Already initialized!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_posixmq_send>               Failed to itci_trans_send(),                    rc = 4!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: posixmq_exit - Not initialized yet!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_posixmq_exit>               Failed to itci_trans_exit(),                    rc = 4!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_posixmq_init>               Calling posixmq_init() successfully,             rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: posixmq_init - Already initialized!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_posixmq_init>               Failed to itci_trans_init(),                    rc = 2!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: posixmq_maxmsgsize - Get max msg size successfully, max_msgsize = 8192!
        DEBUG: start_itcthreads - Starting a thread!
        DEBUG: get_posixmq_cl - Add contact list!
        DEBUG: get_posixmq_id - msgget: No such file or directory
        DEBUG: posixmq_send - Receiver side not initialised message queue yet!

-------------------------------------------------------------------------------------------------------------------
[ABN]:                  <test_posixmq_send>               Receiver side has not init msg queue yet!,      rc = 8!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: terminate_itcthreads - Terminating a thread!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_posixmq_exit>               Calling posixmq_exit() successfully,             rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables
	transporter = posixmq_trans_apis;

	struct itc_mailbox* mbox_1;
	mbox_1 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_1->mbox_id = 0x00600000 | 10;

	struct itc_message* message;
	message = (struct itc_message*)malloc(sizeof(struct itc_message));
	message->msgno = 111;
	message->flags = 1;
	message->receiver = mbox_1->mbox_id;
	message->sender = 0x00500000 | 10;
	message->size = 4;

	PRINT_DASH_END;

	// Test get posixmq system-wide max message queue length in bytes successfully
	(void)test_posixmq_maxmsgsize();								//	-> EXPECT: SUCCESS
	
	// Test send a message when not init yet
	test_posixmq_send(message, mbox_1->mbox_id);						//	-> EXPECT: FAILED
	
	// Test exit when not init yet
	test_posixmq_exit();									//	-> EXPECT: FAILED

	// First time init, ITC_OK									-> EXPECT: SUCCESS
	test_posixmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	
	// Test ITC_ALREADY_INIT									-> EXPECT: FAILED
	test_posixmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);

	// Test send message successfully
	test_posixmq_send(message, mbox_1->mbox_id);						//	-> EXPECT: SUCCESS

	// Test exit message successfully
	test_posixmq_exit();									//	-> EXPECT: SUCCESS

	PRINT_DASH_START;

	free(mbox_1);
	/* Users do not need to free the sending message when transferring over processes.
	* Sysvmq system will help us to free the message after sending to other processes */
	// free(message);

	return 0;
}

void test_posixmq_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_init>\t\t Failed to allocate result_code!\n");
                PRINT_DASH_END;
		return;
	}

	if(transporter.itci_trans_init != NULL)
	{
		transporter.itci_trans_init(rc, my_mbox_id_in_itccoord, itccoord_mask, nr_mboxes, flags);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_posixmq_init>\t\t itci_trans_init = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_init>\t\t Failed to itci_trans_init(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_posixmq_init>\t\t Calling posixmq_init() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_posixmq_exit()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_exit>\t\t Failed to allocate result_code!\n");
                PRINT_DASH_END;
		return;
	}

	if(transporter.itci_trans_exit != NULL)
	{
		transporter.itci_trans_exit(rc);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_posixmq_exit>\t\t itci_trans_exit = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_exit>\t\t Failed to itci_trans_exit(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_posixmq_exit>\t\t Calling posixmq_exit() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_posixmq_send(struct itc_message *message, itc_mbox_id_t to)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_send>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_send != NULL)
	{
		transporter.itci_trans_send(rc, message, to);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_posixmq_send>\t\t itci_trans_send = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		if(rc->flags == ITC_QUEUE_NULL)
		{
			PRINT_DASH_START;
			printf("[ABN]:\t\t\t<test_posixmq_send>\t\t Receiver side has not init msg queue yet!,\t rc = %d!\n", rc->flags);
			PRINT_DASH_END;
			// Because sending failed, user need to self-free the message
			free(message);
		} else
		{
			PRINT_DASH_START;
			printf("[FAILED]:\t\t<test_posixmq_send>\t\t Failed to itci_trans_send(),\t\t\t rc = %d!\n", rc->flags);
			PRINT_DASH_END;
			// Not free the message here due to other causes of sending failures, keep the message for subsequential sends
		}

		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_posixmq_send>\t\t Calling posixmq_send() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

int test_posixmq_maxmsgsize(void)
{
	int res = 0;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_maxmsgsize>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return res;
	}

	if(transporter.itci_trans_maxmsgsize != NULL)
	{
		res = transporter.itci_trans_maxmsgsize(rc);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_posixmq_maxmsgsize>\t itci_trans_maxmsgsize = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return res;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_posixmq_maxmsgsize>\t Failed to itci_trans_maxmsgsize(),\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return res;
	}

	if(res == 0)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t<test_posixmq_maxmsgsize>\t itci_trans_maxmsgsize() return wrong value!\n");
		PRINT_DASH_END;
		free(rc);
		return res;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_posixmq_maxmsgsize>\t posixmq_maxmsgsize() successfully = %d bytes,\t rc = %d!\n", res, rc->flags);
	PRINT_DASH_END;
	free(rc);
	return res;
}

