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