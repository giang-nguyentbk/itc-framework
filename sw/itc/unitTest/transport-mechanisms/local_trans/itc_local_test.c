/* This is some test cases for local transporter's functions
   We will test for interface local trans's functions:
	1. local_init
	2. local_exit
	3. local_create_mbox
	4. local_delete_mbox
	5. local_send
	6. local_receive
	7. local_remove

   Currently, we have no way to test below private functions because they're static function and file scope itc_local.c.
   We will test these below functions via above apis.
        1. find_localmbx_data
        2. create_qitem
        3. remove_qitem
        4. init_queue
        5. enqueue_message
	6. dequeue_message
	7. remove_message_fromqueue
	8. release_localmbx_resources
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "itci_trans.h"
#include "itc.h"
#include "itc_impl.h"

static struct itci_transport_apis transporter;
extern struct itci_transport_apis local_trans_apis;

void test_local_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags);
void test_local_create_mbox(struct itc_mailbox *mailbox, uint32_t flags);
void test_local_send(struct itc_mailbox *mbox, struct itc_message *message, itc_mbox_id_t to, itc_mbox_id_t from);
struct itc_message* test_local_receive(struct itc_mailbox *mbox, long timeout);


/* Expect main call:    ./itc_loca_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-----------------------------------------------------------------------------------
test_local_create_mbox	-> EXPECT: FAILED	rc = 4		-> ITC_NOT_INIT_YET
test_local_receive	-> EXPECT: FAILED	rc = 4		-> ITC_NOT_INIT_YET
test_local_send		-> EXPECT: FAILED	rc = 4		-> ITC_NOT_INIT_YET
-----------------------------------------------------------------------------------
test_local_init		-> EXPECT: SUCCESS	rc = 0		-> ITC_OK
-----------------------------------------------------------------------------------
test_local_init		-> EXPECT: FAILED	rc = 2		-> ITC_ALREADY_INIT
-----------------------------------------------------------------------------------
test_local_init		-> EXPECT: SUCCESS	rc = 0		-> ITC_OK
-----------------------------------------------------------------------------------
test_local_send		-> EXPECT: FAILED	rc = 128	-> ITC_OUT_OF_RANGE
test_local_create_mbox	-> EXPECT: FAILED	rc = 128	-> ITC_OUT_OF_RANGE
-----------------------------------------------------------------------------------
test_local_create_mbox	-> EXPECT: SUCCESS	rc = 0		-> ITC_OK
-----------------------------------------------------------------------------------
test_local_create_mbox	-> EXPECT: FAILED	rc = 1		-> ITC_ALREADY_USED
-----------------------------------------------------------------------------------
test_local_send		-> EXPECT: SUCCESS	rc = 0		-> ITC_OK
-----------------------------------------------------------------------------------
test_local_receive	-> EXPECT: FAILED	rc = 64		-> ITC_NOT_THIS_PROC
-----------------------------------------------------------------------------------
*/


	transporter = local_trans_apis;

/*********************************/
/*** Test local_create_mailbox ***/
/*********************************/
	// Test create mailbox when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	struct itc_mailbox* mbox_1;
	mbox_1 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_1->mbox_id = 0x00500000 | 10;
	test_local_create_mbox(mbox_1, 0);

/*********************************/
/***     Test local_receive    ***/
/*********************************/
	// Test receive a ITC message when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	test_local_receive(mbox_1, 0);

/*********************************/
/***      Test local_send      ***/
/*********************************/
	// Test send a ITC message when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	struct itc_message* message;
	message = (struct itc_message*)malloc(sizeof(struct itc_message));
	message->msgno = 111;
	test_local_send(mbox_1, message, (itc_mbox_id_t)(0x00500000 | 10), (itc_mbox_id_t)0);

/*********************************/
/***      Test local_init      ***/
/*********************************/
	// First time init, ITC_OK								-> EXPECT: SUCCESS
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_ALREADY_INIT								-> EXPECT: FAILED
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_FLAGS_FORCE_REINIT								-> EXPECT: FAILED
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)5, (uint32_t)ITC_FLAGS_FORCE_REINIT);
	
/*********************************/
/***      Test local_send      ***/
/*********************************/
	// Test send a ITC message but mailbox was not created					-> EXPECT: FAILED
	test_local_send(mbox_1, message, (itc_mbox_id_t)(0x00500000 | 10), (itc_mbox_id_t)0);

/*********************************/
/*** Test local_create_mailbox ***/
/*********************************/
	// Test create mailbox with a mailbox id out of range (100 > mask(5) + 1 = 7 + 1 = 8)	-> EXPECT: FAILED
	mbox_1->mbox_id = 0x00500000 | 100;
	test_local_create_mbox(mbox_1, 0);
	// Test create mailbox with a mailbox id in range successfully				-> EXPECT: SUCCESS
	mbox_1->mbox_id = 0x00500000 | 5;
	test_local_create_mbox(mbox_1, 0);
	// Test create mailbox with a mailbox id already used					-> EXPECT: FAILED
	struct itc_mailbox* mbox_2;
	mbox_2 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_2->mbox_id = 0x00500000 | 5;
	test_local_create_mbox(mbox_2, 0);
 
/*********************************/
/***      Test local_send      ***/
/*********************************/
	// Test send a ITC message successfully							-> EXPECT: SUCCESS
	test_local_send(mbox_2, message, (itc_mbox_id_t)(0x00500000 | 5), (itc_mbox_id_t)0);

/*********************************/
/***     Test local_receive    ***/
/*********************************/
	// Test receive a ITC message with wrong my_mbox_id_in_itccoord
	//struct itc_message* received_message;
	mbox_2->mbox_id = 0x00600000 | 5; // Expected is 0x00500000 | 5
	test_local_receive(mbox_2, 0);
	//received_message = test_local_receive(mbox_2, 0);

	free(mbox_1);
	free(mbox_2);
	free(message);
        return 0;
}


void test_local_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_local_init>\t\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_init != NULL)
	{
		transporter.itci_trans_init(rc, my_mbox_id_in_itccoord, itccoord_mask, nr_mboxes, flags);
	} else
        {
                printf("[FAILED]:\t<test_local_init>\t\t itci_trans_init = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_local_init>\t\t Failed to itci_trans_init(),\t\t\t rc = %d!\n", rc->flags);
		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_local_init>\t\t Calling local_init() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}

void test_local_create_mbox(struct itc_mailbox *mailbox, uint32_t flags)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_local_create_mbox>\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_create_mbox != NULL)
	{
		transporter.itci_trans_create_mbox(rc, mailbox, flags);
	} else
        {
                printf("[FAILED]:\t<test_local_create_mbox>\t\t itci_trans_create_mbox = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_local_create_mbox>\t Failed to itci_trans_create_mbox(),\t\t rc = %d!\n", \
			rc->flags);
		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_local_create_mbox>\t Calling local_create_mbox() successfully,\t rc = %d!\n", \
		rc->flags);
	free(rc);
}

void test_local_send(struct itc_mailbox *mbox, struct itc_message *message, itc_mbox_id_t to, itc_mbox_id_t from)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_local_send>\t\t Failed to allocate result_code!\n");
                return;
	}

	if(transporter.itci_trans_send != NULL)
	{
		transporter.itci_trans_send(rc, mbox, message, to, from);
	} else
        {
                printf("[FAILED]:\t<test_local_send>\t\t itci_trans_send = NULL!\n");
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_local_send>\t\t Failed to itci_trans_send(),\t\t\t rc = %d!\n", rc->flags);
		free(rc);
		return;
	}

	printf("[SUCCESS]:\t\t<test_local_send>\t\t Calling local_send() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}

struct itc_message* test_local_receive(struct itc_mailbox *mbox, long timeout)
{
	struct itc_message* ret_message;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		printf("[FAILED]:\t\t<test_local_receive>\t\t Failed to allocate result_code!\n");
                return NULL;
	}

	if(transporter.itci_trans_receive != NULL)
	{
		ret_message = transporter.itci_trans_receive(rc, mbox, timeout);
	} else
        {
                printf("[FAILED]:\t<test_local_receive>\t\t itci_trans_receive = NULL!\n");
		free(rc);
                return NULL;
        }

	if(rc->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<test_local_receive>\t\t Failed to itci_trans_receive(),\t\t rc = %d!\n", \
			rc->flags);
		free(rc);
		return NULL;
	}

	printf("[SUCCESS]:\t\t<test_local_receive>\t\t Calling local_receive() successfully,\t\t rc = %d!\n", \
		rc->flags);
	free(rc);

	return ret_message;
}