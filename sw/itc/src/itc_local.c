/* Okay, let's put itc API declarations aside, we can easily start with an implementaion for local transportation
mechanism.

First, let's create an generic interface for transportation mechanisms. Our local trans is just one implementation of
that interface. To create a set of APIs in C, it's great because we can have a struct with a soft of function pointers
inside. Definitions for those function pointers (APIs) is implemented by local_trans, sock_trans or sysv_trans.

For example:
struct itci_trans_apis {
    itci_init               *itci_init;
    itci_create_mailbox     *itci_create_mailbox;
    itci_send               *itci_send;
    itci_receive            *itci_receive;
    ...
};

Which functions to be done:
    First have to implement above api functions.

    Additionally, we also need some private functions:
    + enqueue()
    + dequeue()
    + ...
*/


#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"

/*****************************************************\/
*   FUNCTION PROTOTYPES AND STATIC GLOBAL VARIABLES    *
*******************************************************/
static int local_send(  struct itc_mailbox  *mbox,
                        struct itc_message  *message,
                        itc_mbox_id_t       to,
                        itc_mbox_id_t       from);

static struct itc_message *local_receive(   struct itc_mailbox  *mbox,
                                            const uint32_t      *filter,
                                            long                tmo,
                                            itc_mbox_id_t       from,
                                            bool                recursive_call);

struct itci_trans_apis local_trans_apis = { local_send,
                                            local_receive };


/*****************************************************\/
*                  FUNCTION DEFINITIONS                *
*******************************************************/
static int local_send(  struct itc_mailbox  *mbox,
                        struct itc_message  *message,
                        itc_mbox_id_t       to,
                        itc_mbox_id_t       from)
{

}
