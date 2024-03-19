#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "itci_trans.h"
#include "itc.h"
#include "itc_impl.h"
#include "itc_threadmanager.h"

static struct itci_transport_apis transporter;
extern struct itci_transport_apis sysvmq_trans_apis;

void test_sysvmq_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags);
void test_sysvmq_exit(void);
void test_sysvmq_send(struct itc_message *message, itc_mbox_id_t to);
int test_sysvmq_maxmsgsize(void);

/* Helper functions */
void start_itcthreads_helper(void);
void terminate_itcthreads_helper(void);


/* Expect main call:    ./itc_sysvmq_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_sysvmq_maxmsgsize>         sysvmq_maxmsgsize() successfully = 8192 bytes,  rc = 0!
[FAILED]:               <test_sysvmq_send>               Failed to itci_trans_send(),                    rc = 4!
[FAILED]:               <test_sysvmq_exit>               Failed to itci_trans_exit(),                    rc = 4!
[SUCCESS]:              <test_sysvmq_init>               Calling sysvmq_init() successfully,             rc = 0!
[FAILED]:               <test_sysvmq_init>               Failed to itci_trans_init(),                    rc = 2!
[ABN]:                  <test_sysvmq_send>               Receiver side has not init msg queue yet!,      rc = 16!
[SUCCESS]:              <test_sysvmq_exit>               Calling sysvmq_exit() successfully,             rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables
	transporter = sysvmq_trans_apis;

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

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

	(void)test_sysvmq_maxmsgsize();
	test_sysvmq_send(message, mbox_1->mbox_id);						//	-> EXPECT: FAILED
	test_sysvmq_exit();									//	-> EXPECT: FAILED

	// First time init, ITC_OK									-> EXPECT: SUCCESS
	test_sysvmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_ALREADY_INIT									-> EXPECT: FAILED
	test_sysvmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	
	start_itcthreads_helper();

	test_sysvmq_send(message, mbox_1->mbox_id);						//	-> EXPECT: SUCCESS

	terminate_itcthreads_helper();

	test_sysvmq_exit();									//	-> EXPECT: SUCCESS

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

	free(mbox_1);
	/* Users do not need to free the sending message when transferring over processes.
	* Sysvmq system will help us to free the message after sending to other processes */
	// free(message);

	return 0;
}

void test_sysvmq_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_sysvmq_init>\t\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_init != NULL)
	{
		transporter.itci_trans_init(rc, my_mbox_id_in_itccoord, itccoord_mask, nr_mboxes, flags);
	} else
        {
                printf("[FAILED]:\t<test_sysvmq_init>\t\t itci_trans_init = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_sysvmq_init>\t\t Failed to itci_trans_init(),\t\t\t rc = %d!\n", rc->flags);
		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_sysvmq_init>\t\t Calling sysvmq_init() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}

void test_sysvmq_exit()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_sysvmq_exit>\t\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_exit != NULL)
	{
		transporter.itci_trans_exit(rc);
	} else
        {
                printf("[FAILED]:\t<test_sysvmq_exit>\t\t itci_trans_exit = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_sysvmq_exit>\t\t Failed to itci_trans_exit(),\t\t\t rc = %d!\n", rc->flags);
		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_sysvmq_exit>\t\t Calling sysvmq_exit() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}

void test_sysvmq_send(struct itc_message *message, itc_mbox_id_t to)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_sysvmq_send>\t\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_send != NULL)
	{
		transporter.itci_trans_send(rc, message, to);
	} else
        {
                printf("[FAILED]:\t<test_sysvmq_send>\t\t itci_trans_send = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		if(rc->flags == ITC_RX_QUEUE_NULL)
		{
			printf("[ABN]:\t\t\t<test_sysvmq_send>\t\t Receiver side has not init msg queue yet!,\t rc = %d!\n", rc->flags);
			// Because sending failed, user need to self-free the message
			free(message);
		} else
		{
			printf("[FAILED]:\t\t<test_sysvmq_send>\t\t Failed to itci_trans_send(),\t\t\t rc = %d!\n", rc->flags);
			// Not free the message here due to other causes of sending failures, keep the message for subsequential sends
		}

		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_sysvmq_send>\t\t Calling sysvmq_send() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}

int test_sysvmq_maxmsgsize(void)
{
	int res = 0;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_sysvmq_maxmsgsize>\t Failed to allocate result_code!\n");
                return res;
	}

	if(transporter.itci_trans_maxmsgsize != NULL)
	{
		res = transporter.itci_trans_maxmsgsize(rc);
	} else
        {
                printf("[FAILED]:\t<test_sysvmq_maxmsgsize>\t itci_trans_maxmsgsize = NULL!\n");
		free(rc);
                return res;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_sysvmq_maxmsgsize>\t Failed to itci_trans_maxmsgsize(),\t\t rc = %d!\n", rc->flags);
		free(rc);
		return res;
	}

	if(res == 0)
	{
		printf("[FAILED]:\t<test_sysvmq_maxmsgsize>\t itci_trans_maxmsgsize() return wrong value!\n");
	}

	printf("[SUCCESS]:\t\t<test_sysvmq_maxmsgsize>\t sysvmq_maxmsgsize() successfully = %d bytes,\t rc = %d!\n", res, rc->flags);
	free(rc);
	return res;
}

void start_itcthreads_helper()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<start_itcthreads_helper>\t Failed to allocate result_code!\n");
                return;
	}

	start_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<start_itcthreads_helper>\t Failed to start_itcthreads!\n");
	}

	free(rc);
}

void terminate_itcthreads_helper()
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<terminate_itcthreads_helper>\t Failed to allocate result_code!\n");
                return;
	}

	terminate_itcthreads(rc);
	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<terminate_itcthreads_helper>\t Failed to start_itcthreads!\n");
	}

	free(rc);
}