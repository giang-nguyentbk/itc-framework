/* This is some test cases for local transporter's functions
   We will test for interface local trans's functions:
	1. local_init
	2. local_exit
	3. local_create_mbox
	4. local_delete_mbox
	5. local_send
	6. local_receive
	7. local_remove

   Currently, we have no way to test below private functions because they're static function and file scope itc_local.c
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

int test_local_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags);
int test_local_create_mbox(struct itc_mailbox *mailbox, uint32_t flags);


/* Expect main call:    ./itc_loca_test */
int main(int argc, char* argv[])
{
	transporter = local_trans_apis;

	/* Test local_create_mailbox */
	// Test create mailbox when ITC local transport was not initialised yet
	struct itc_mailbox* mbox_1;
	mbox_1 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_1->mbox_id = 0x00500000 | 10;
	test_local_create_mbox(mbox_1, 0);

        /* Test local_init */
	// First time init, not ITC_FLAGS_FORCE_REINIT
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_RET_INIT_ALREADY_INIT
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_FLAGS_FORCE_REINIT
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)5, (uint32_t)ITC_FLAGS_FORCE_REINIT);
	
	/* Test local_create_mailbox */
	// Test create mailbox with a mailbox id out of range (10 > mask(5) + 1 = 7 + 1 = 8)
	test_local_create_mbox(mbox_1, 0);
	// Test create mailbox with a mailbox id in range successfully
	mbox_1->mbox_id = 0x00500000 | 5;
	test_local_create_mbox(mbox_1, 0);
	// Test create mailbox with a mailbox id already used
	struct itc_mailbox* mbox_2;
	mbox_2 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_2->mbox_id = 0x00500000 | 5;
	test_local_create_mbox(mbox_2, 0);

	free(mbox_1);
	free(mbox_2);
        return 0;
}


int test_local_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags)
{
	int ret = 0;
	if(transporter.itci_trans_init != NULL)
	{
		ret = transporter.itci_trans_init(my_mbox_id_in_itccoord, itccoord_mask, nr_mboxes, flags);
	}

	if(ret == ITC_RET_INIT_ALREADY_INIT)
	{
		printf("[SUCCESS]:\t\t<test_local_init>\t\tITC local transport was already init!\n");
	} else if(ret == ITC_RET_INIT_OUT_OF_MEM)
	{
		printf("[FAILED]:\t\t<test_local_init>\t\tFailed to allocate RX queue for ITC local tranposrt, OOM!\n");
	} else if(ret == ITC_RET_OK && flags != ITC_FLAGS_FORCE_REINIT)
	{
		printf("[SUCCESS]:\t\t<test_local_init>\t\tInit ITC local transport successfully!\n");
	} else if(ret == ITC_RET_OK && flags == ITC_FLAGS_FORCE_REINIT)
	{
		printf("[SUCCESS]:\t\t<test_local_init>\t\tRe-init ITC local transport successfully!\n");
	} else
	{
		printf("[FAILED]:\t\t<test_local_init>\t\tUndefined failed!\n");
	}

	return ret;
}

int test_local_create_mbox(struct itc_mailbox *mailbox, uint32_t flags)
{
	int ret = 0;
	if(transporter.itci_trans_create_mbox != NULL)
	{
		ret = transporter.itci_trans_create_mbox(mailbox, flags);
	}

	if(ret == ITC_RET_INIT_NOT_INIT_YET)
	{
		printf("[FAILED]:\t\t<test_local_create_mbox>\tITC local transport was not init yet!\n");
	} else if(ret == ITC_RET_INIT_ALREADY_USED)
	{
		printf("[FAILED]:\t\t<test_local_create_mbox>\tThe mailbox id was used by someone!\n");
	} else if(ret == ITC_RET_INIT_OUT_OF_MEM)
	{
		printf("[FAILED]:\t\t<test_local_create_mbox>\tFailed to create new mailbox, OOM!\n");
	} else if(ret == ITC_RET_OK)
	{
		printf("[SUCCESS]:\t\t<test_local_create_mbox>\tCreate new mailbox successfully!\n");
	} else
	{
		printf("[FAILED]:\t\t<test_local_init>\t\tUndefined failed!\n");
	}
}
