# itc-framework
```bash
How many lines of code in this repo?
$ git ls-files | grep "\(.c\|.sig\|.h\)$" | xargs wc -l
-> Total: 3007 lines (updated 17/3/2024)
-> Total: 5757 lines (updated 24/3/2024)
-> Total: 6582 lines (updated 25/3/2024)
-> Total: 7257 lines (updated 26/3/2024)
```

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

![](./assets/mailbox-id.png?raw=true)
```
Mailbox ID will be formed like the figure above. Number of bit "m" is fixed as 12 bits and used for itc_coord process id
(show how many processes are running). Number of bit "n" is depending on the number of mailboxes you want
(show how many mailboxes in a process being used).

For example, if you want this process to have 100 mailboxes -> we will use 7 right-most bits 0x7F (127) -> n = 7.
Maximum n = 16 bits, because we did set ITC_MAX_MAILBOXES = 65534.
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
There is a concept called itc_coordinator or just itc_coord which plays a role as a representative or contact point
to orchestrate all itc activities within a board. This must be run before any other programs that use ITC can start.

This is the first contact point for itc_init() at ITC initialization for a process.

At ITC initialization itc_init() of a process, we must locate the itc_coord to get information:
your mailbox id in itc_coord, itc_coord_mask, itc_coord mailbox id.

After initialization, whenever a mailbox is created, it must notify itc_coord by sending a message. Similarly, when
a mailbox or the process is terminated, you also need to inform itc_coord about that.

It will monitor "alive" status for registered mailboxes of each process and make sure to remove all related mailboxes
when a process dies.

It will be implemented as a UNIX socket which is opened at /var/usr/itc_coord/itc_coord_locator.
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
        queue length are limited. We will design a small algorithm that will prioritize sysvmq once itc message
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

### 4. Unit Test
+ Generalised Malloc:

![](./assets/malloc-UT.png?raw=true)

+ Local transportation:

![](./assets/local-UT.png?raw=true)

+ Sysvmq transportation:

![](./assets/sysvmq-UT.png?raw=true)

+ Thread manager:

![](./assets/thread-man-UT.png?raw=true)

+ Queue:

![](./assets/queue-UT.png?raw=true)

### 5. Future Improvements
```
1. One message can be only sent to one receiver:
	+ The current implementation is implicitly understood as Single Input Single Out SISO, which means a sender
	can only send a message to one receiver, not to multiple receivers.

	+ Since to send a message to multiple mailboxes, we need to re-design message deallocation mechanism. Currently,
	a sender calls itc_alloc() -> itc_send(), whereas a receiver will constantly call itc_receive() in
	an infinite loop to expect the message, then handle it and call itc_free() to deallocate it.

	+ So, what happens when we send a message to many receivers? Who will call itc_free()? That's the problem.

	+ We will re-design it so that receivers will pass responsibilities about deallocating the message to the sender
	
	+ When the message is not in any rx queues of any mailboxes, meaning all receivers have dequeued the message
	from their queue, sender will call itc_free() to free the message.
	
	+ But, shall sender have to wait for this? Sender will be blocked? There must be some smart way to implement
	this asynchronously.

2. In local transportation, need some way to manage rx queue more reliable such as max items in queue, auto clean up
messages in queue which is not dequeued for a long time,...

3. No message filter for itc_receive() currently:
	+ We can filter which message types you want to get. Param filter is an array with:
                filter[0] = how many message types you want to get.
                filter[1] = msgno1
                filter[2] = msgno2
                filter[3] = msgno3
                ...

	+ We may want to get messages from someone only, or get from all mailboxes via ITC_FROM_ALL.

4. In the future, itc.c will manage all active/non-active mailboxes via a red-black tree that is easier for a mailbox to locate another one. Each node in the tree will contain the data of a mailbox such as name, mailbox id, fd,...

5. Namespace will be implemeted later


----> All improvements will be soon committed in ITC V2.
```

