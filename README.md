# itc-framework

## Fundamental knowledge:
### 1. General architecture:

![](./assets/itc-architecture.png?raw=true)
```
This is an implementation that allows exchanging user messages between threads, processes and even boards.

Each board is one instance of a single OS. Each board may have several processes.

Each process must initialize itc infrastructure before any itc calls can be performed.

Each threads (inside a process) must create a mailbox (probably one per thread) with a unique mailbox id across
the entire universe (your whole system including all boards, devices,...).

Each mailbox can have a mailbox name (char *) and some aliases, but only one mailbox id. This is same as that
you can have several names (use at school, at home,...), but you are you (same as the number
in your Citizen Identity Card).
```

![](./assets/itc-message.png?raw=true)
```
ITC messages includes two parts:
        + First is the ITC header which consists of pointer to the next and previous
        messages, a flags to indicate message state (such as it's in rx state,...), who is sender and receiver,
        size of the itc msg. This is only used for internal control purposes by the framework.

        + Second is the ITC msg which will see and be used by users. They just need to define their own sets of itc msg
        where every msg number must be unique across the whole universe.
```

```
There is a concept called itc_coordinator or just itc_coor which plays a role as a representative or contact point
to orchestrate all itc activities within a board. This must be run before any other programs that use ITC can start.

This is the first contact point for any ITC mailbox at initialization.

Each mailbox when created must locate the itc_coor. It will monitor "alive" status for registered mailboxes
and make sure to remove all related mailboxes when a process dies. It will be implemented as a UNIX socket which is
opened at /var/usr/itc_coor/itc_coor_locator.
```
```
To communicate over hosts (different boards), we will need AF_INET TCP/UDP IP socket, rather than AF_UNIX/AF_LOCAL
Unix domain socket. There is a special mailbox called itc_gateway responsible for opening a IP socket
that can be connected from anywhere, translating between itc_msg and network packets,
converting network-byte-order from/to host-byte-order.

Any mailbox inside a host that wants to send messages to an external mailbox must send the itc_msg to itc_gateway first.
```
```
There is a definition called namespace. This allows mailbox can have same names but different namespace
(or different path).

For example, an mailbox should have a full name (path) like this: /board_1/process_1/mailbox_1.

Based on namespace, it's easier for mailbox to select which trans function should be used for the best convenience.
```
### 2. Transportation mechanism:
```
        + Local trans: used for inter-thread communication. Because threads inside a process will share
        a same virtual address space. We can easily implement a rx queue for each single mailbox. Sender will enqueue
        a message to receiver's rx queue and receiver will dequeue it then. There are some synchronization
        approaches needed. For example, after putting a message to receiver's queue, sender needs to do some way
        to notify it to receiver. We will use pthread condition variables as a synchronous way, and epoll/eventfd
        for a async way.

        + Unix socket: used for inter-process and inter-host communication. There are two types of unix socket.
        First is abstract/anonymous sockets which is automatically cleaned up by OS if noboday references to it.
        Second is file-based/regular unix sockets which user has to manually clean it up.

        + System V message queue: used for inter-process communication only. Faster than socket, but message length and
        queue length are limited. We will design a small algorithm that will prioritize sysv once itc message
        is not large.
```

### 3. Memory allocation mechanism:
```
        + Generalised Malloc (default): to allocate any size (bytes) of itc messages.

        + Memory Pool: to pre-allocate memory blocks with fixed sizes such as 32, 224, 992, 4064, 16352, 65504 bytes.
        Will be implemented later. Same as Paging which is done by OS, if you keep allocating memory in heap
        via malloc ordinarily, your heap will be quickly fragmented. Instead of that, using fixed size of memory blocks,
        also called pools, which will help you efficiently ultilize heap memories.
```

![](./assets/malloc-unitTest.png?raw=true)

