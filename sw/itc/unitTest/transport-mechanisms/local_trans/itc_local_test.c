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
extern struct itci_transport_apis local_trans_apis;

void test_local_init(itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, int nr_mboxes, uint32_t flags);
void test_local_exit(void);
void test_local_create_mbox(struct itc_mailbox *mailbox, uint32_t flags);
void test_local_delete_mbox(struct itc_mailbox *mailbox);
void test_local_send(struct itc_message *message, itc_mbox_id_t to);
struct itc_message* test_local_receive(struct itc_mailbox *my_mbox);
struct itc_message* test_local_remove(struct itc_mailbox *mbox, struct itc_message *removed_message);


/* Expect main call:    ./itc_local_test */
int main(int argc, char* argv[])
{
/* TEST EXPECTATION:
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Not initialized yet!
        DEBUG: local_create_mbox - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_create_mbox>         Failed to itci_trans_create_mbox(),             rc = 4!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Not initialized yet!
        DEBUG: local_receive - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_receive>             Failed to itci_trans_receive(),                 rc = 4!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Not initialized yet!
        DEBUG: local_send - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_send>                Failed to itci_trans_send(),                    rc = 4!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_exit - Not initialized yet, but it's ok to exit!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_exit>                Calling local_exit() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_init>                Calling local_init() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_init - Already initialized!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_init>                Failed to itci_trans_init(),                    rc = 2!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_init - Force re-initializing!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_init>                Calling local_init() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Mailbox ID exceeded nr_mboxes, local mbox_id = 10, nr_mboxes = 8!
        DEBUG: local_send - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_send>                Failed to itci_trans_send(),                    rc = 64!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Mailbox ID exceeded nr_mboxes, local mbox_id = 100, nr_mboxes = 8!
        DEBUG: local_create_mbox - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_create_mbox>         Failed to itci_trans_create_mbox(),             rc = 64!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_remove - Already used by another mailbox!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_remove>              Failed to itci_trans_remove(),                  rc = 8!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_delete_mbox - Already used by another mailbox!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_delete_mbox>         Failed to itci_trans_delete_mbox(),             rc = 8!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_create_mbox>         Calling local_create_mbox() successfully,       rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_create_mbox - Already used by another mailbox!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_create_mbox>         Failed to itci_trans_create_mbox(),             rc = 1!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: enqueue_message - RX queue has only one item, add a new one!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_send>                Calling local_send() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: find_localmbx_data - Not belong to this process!
        DEBUG: local_receive - Not belong to this process!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_receive>             Failed to itci_trans_receive(),                 rc = 32!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: dequeue_message - RX queue has only one item, dequeue it!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_receive>             Calling local_receive() successfully,           rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: remove_message_fromqueue - Message not found!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_remove>              Failed to itci_trans_remove(),                  rc = 16!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: enqueue_message - RX queue has only one item, add a new one!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_send>                Calling local_send() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: remove_message_fromqueue - Item found!
        DEBUG: remove_message_fromqueue - Queue empty!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_remove>              Calling local_remove() successfully,            rc = 0!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: local_exit - Still had 1 remaining open mailboxes!

-------------------------------------------------------------------------------------------------------------------
[FAILED]:               <test_local_exit>                Failed to itci_trans_exit(),                    rc = 128!
-------------------------------------------------------------------------------------------------------------------

        DEBUG: dequeue_message - RX queue is empty!

-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_delete_mbox>         Calling local_delete_mbox() successfully,       rc = 0!
-------------------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------------
[SUCCESS]:              <test_local_exit>                Calling local_exit() successfully,              rc = 0!
-------------------------------------------------------------------------------------------------------------------
*/

	(void)argc; // Avoid compiler warning unused variables
	(void)argv; // Avoid compiler warning unused variables
	transporter = local_trans_apis;


	struct itc_message* message;
	message = (struct itc_message*)malloc(sizeof(struct itc_message));
	message->msgno = 111;

	struct itc_mailbox* mbox_1;
	mbox_1 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_1->mbox_id = 0x00500000 | 10;

	struct itc_mailbox* mbox_2;
	mbox_2 = (struct itc_mailbox*)malloc(sizeof(struct itc_mailbox));
	mbox_2->mbox_id = 0x00500000 | 5;

	PRINT_DASH_END;

	// Test create mailbox when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	
	test_local_create_mbox(mbox_1, 0);
	// Test receive a ITC message when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	test_local_receive(mbox_1);
	// Test send a ITC message when ITC local transport was not ITC_NOT_INIT_YET		-> EXPECT: FAILED
	
	test_local_send(message, (itc_mbox_id_t)(0x00500000 | 10));


	// Test exit when not init yet								-> EXPECT: FAILED
	test_local_exit();
	// First time init, ITC_OK								-> EXPECT: SUCCESS
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);
	// Test ITC_ALREADY_INIT								-> EXPECT: FAILED
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)100, (uint32_t)0);


	// Test ITC_FLAGS_FORCE_REINIT								-> EXPECT: SUCCESS
	test_local_init((itc_mbox_id_t)0x00500000, (itc_mbox_id_t)0xFFF00000, (int)5, (uint32_t)ITC_FLAGS_FORCE_REINIT);
	// Test send a ITC message but mailbox id is out of range				-> EXPECT: FAILED
	test_local_send(message, (itc_mbox_id_t)(0x00500000 | 10));
	// Test create mailbox with a mailbox id out of range (100 > mask(5) + 1 = 7 + 1 = 8)	-> EXPECT: FAILED
	mbox_1->mbox_id = 0x00500000 | 100;
	test_local_create_mbox(mbox_1, 0);


	// Test remove message in rx queue which is NULL					-> EXPECT: FAILED
	mbox_1->mbox_id = 0x00500000 | 3; // Mailbox_id 3 is not created yet
	test_local_remove(mbox_1, message);
	// Test delete mailbox which has not been created ITC_QUEUE_NULL			-> EXPECT: FAILED
	test_local_delete_mbox(mbox_1);
	// Test create mailbox with a mailbox id in range successfully				-> EXPECT: SUCCESS
	mbox_1->mbox_id = 0x00500000 | 5;
	test_local_create_mbox(mbox_1, 0);


	// Test create mailbox with a mailbox id already used					-> EXPECT: FAILED
	test_local_create_mbox(mbox_2, 0);
	// Test send a ITC message successfully							-> EXPECT: SUCCESS
	test_local_send(message, (itc_mbox_id_t)(0x00500000 | 5));
	// Test receive a ITC message but it's not for this process				-> EXPECT: FAILED
	mbox_2->mbox_id = 0x00600000 | 5; // Expected is 0x00500000 | 5
	test_local_receive(mbox_2);


	// Test receive a ITC message successfully						-> EXPECT: SUCCESS
	test_local_receive(mbox_1);
	// Test remove message in rx queue which is empty					-> EXPECT: FAILED
	test_local_remove(mbox_1, message);
	// This is served for next test_local_remove()						-> EXPECT: SUCCESS
	test_local_send(message, (itc_mbox_id_t)(0x00500000 | 5));


	// Test remove message in rx queue which has been enqueued above by local_send()	-> EXPECT: SUCCESS
	test_local_remove(mbox_1, message);

	// Test exit when not delete all mailboxes yet						-> EXPECT: FAILED
	test_local_exit();
	// Test delete mailbox successfully		 					-> EXPECT: SUCCESS
	test_local_delete_mbox(mbox_1);


	// Test exit successfully								-> EXPECT: SUCCESS
	test_local_exit();

	PRINT_DASH_START;

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
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_init>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_init != NULL)
	{
		transporter.itci_trans_init(rc, my_mbox_id_in_itccoord, itccoord_mask, nr_mboxes, flags);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_init>\t\t itci_trans_init = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_init>\t\t Failed to itci_trans_init(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_init>\t\t Calling local_init() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_local_exit(void)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_exit>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_exit != NULL)
	{
		transporter.itci_trans_exit(rc);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_exit>\t\t itci_trans_exit = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_exit>\t\t Failed to itci_trans_exit(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_exit>\t\t Calling local_exit() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
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
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_create_mbox>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_create_mbox != NULL)
	{
		transporter.itci_trans_create_mbox(rc, mailbox, flags);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_create_mbox>\t\t itci_trans_create_mbox = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_create_mbox>\t Failed to itci_trans_create_mbox(),\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_create_mbox>\t Calling local_create_mbox() successfully,\t rc = %d!\n", \
		rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_local_delete_mbox(struct itc_mailbox *mailbox)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_delete_mbox>\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_delete_mbox != NULL)
	{
		transporter.itci_trans_delete_mbox(rc, mailbox);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_delete_mbox>\t\t itci_trans_delete_mbox = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_delete_mbox>\t Failed to itci_trans_delete_mbox(),\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_delete_mbox>\t Calling local_delete_mbox() successfully,\t rc = %d!\n", \
		rc->flags);
	PRINT_DASH_END;
	free(rc);
}

void test_local_send(struct itc_message *message, itc_mbox_id_t to)
{
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_send>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return;
	}

	if(transporter.itci_trans_send != NULL)
	{
		transporter.itci_trans_send(rc, message, to);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_send>\t\t itci_trans_send = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_send>\t\t Failed to itci_trans_send(),\t\t\t rc = %d!\n", rc->flags);
		PRINT_DASH_END;
		free(rc);
		return;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_send>\t\t Calling local_send() successfully,\t\t rc = %d!\n", rc->flags);
	PRINT_DASH_END;
	free(rc);
}

struct itc_message* test_local_receive(struct itc_mailbox *my_mbox)
{
	struct itc_message* ret_message;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_receive>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return NULL;
	}

	if(transporter.itci_trans_receive != NULL)
	{
		ret_message = transporter.itci_trans_receive(rc, my_mbox);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_receive>\t\t itci_trans_receive = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return NULL;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_receive>\t\t Failed to itci_trans_receive(),\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return NULL;
	}

	PRINT_DASH_START;
	printf("[SUCCESS]:\t\t<test_local_receive>\t\t Calling local_receive() successfully,\t\t rc = %d!\n", \
		rc->flags);
	PRINT_DASH_END;
	free(rc);

	return ret_message;
}

struct itc_message* test_local_remove(struct itc_mailbox *mbox, struct itc_message *removed_message)
{
	struct itc_message* rmv_message;
	struct result_code* rc = (struct result_code*)malloc(sizeof(struct result_code));
	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_remove>\t\t Failed to allocate result_code!\n");
		PRINT_DASH_END;
                return NULL;
	}

	if(transporter.itci_trans_remove != NULL)
	{
		rmv_message = transporter.itci_trans_remove(rc, mbox, removed_message);
	} else
        {
		PRINT_DASH_START;
                printf("[FAILED]:\t<test_local_remove>\t\t itci_trans_remove = NULL!\n");
		PRINT_DASH_END;
		free(rc);
                return NULL;
        }

	if(rc->flags != ITC_OK)
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_remove>\t\t Failed to itci_trans_remove(),\t\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
		free(rc);
		return NULL;
	}

	if(rmv_message == removed_message)
	{
		PRINT_DASH_START;
		printf("[SUCCESS]:\t\t<test_local_remove>\t\t Calling local_remove() successfully,\t\t rc = %d!\n", \
			rc->flags);
		PRINT_DASH_END;
	} else
	{
		PRINT_DASH_START;
		printf("[FAILED]:\t\t<test_local_remove>\t\t Failed due to returned removed message not equal to \n" \
			"requested removed message!");
		PRINT_DASH_END;
	}

	free(rc);

	return rmv_message;
}