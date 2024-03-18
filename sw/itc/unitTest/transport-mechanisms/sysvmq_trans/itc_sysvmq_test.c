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



/* Expect main call:    ./itc_sysvmq_test */
int main(int argc, char* argv[])
{
	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables
	transporter = sysvmq_trans_apis;

	struct result_code* rc_tmp;

	printf("--------------------------------------------------------------------------------------" \
		"-----------------------------\n");

	// First time init, ITC_OK								-> EXPECT: SUCCESS
	test_sysvmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_ALREADY_INIT								-> EXPECT: FAILED
	test_sysvmq_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);

	rc_tmp = (struct result_code*)malloc(sizeof(struct result_code));

	rc_tmp->flags = ITC_OK;
	start_itcthreads(rc_tmp);
	if(rc_tmp->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<start_itcthreads>\t\t Failed to start_itcthreads!\n");
	}

	rc_tmp->flags = ITC_OK;
	terminate_itcthreads(rc_tmp);
	if(rc_tmp->flags != ITC_OK)
	{
		printf("[FAILED]:\t\t<terminate_itcthreads>\t\t Failed to terminate_itcthreads!\n");
	}
	free(rc_tmp);

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

	printf("[SUCCESS]:\t\t<test_sysvmq_init>\t\t Calling local_init() successfully,\t\t rc = %d!\n", rc->flags);
	free(rc);
}