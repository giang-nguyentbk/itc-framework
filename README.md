# itc-framework

## Fundamental knowledge:
### 1. General architecture:

![](./assets/itc-architecture.png?raw=true)

This is an implementation that allows exchanging user messages between threads, processes and even boards.

Each board is one instance of a single OS. Each board may have several processes.

Each process must initialize itc infrastructure before any procedures can be started.

Each threads (inside a process) must create an mailbo (probably one per thread) with an unique mailbox id.

Each mailbox can have a mailbox name (char *) and some aliases, but only one mailbox id. This is same as that
you can have several names (use at school, at home,...), but you are you (same as the number
in your Citizen Identity Card).


### 2. Transportation mechanism:
    + Local trans: used for inter-thread communication. Because threads inside a process will share
    a same virtual address space. We can easily implement a rx queue for each single mailbox. Sender will enqueue
    a message to receiver's rx queue and receiver will dequeue it then. There are some synchronization
    approaches needed. For example, after putting a message to receiver's queue, sender needs to do some way to notify
    it to receiver. We will use pthread condition variables as a synchronous way, and epoll/eventfd for a async way.
    
    + Unix socket: used for inter-process and inter-host communication. There are two types of unix socket.
    First is abstract/anonymous sockets which is automatically cleaned up by OS if noboday references to it.
    Second is file-based/regular unix sockets which user has to manually clean it up.
    
    + System V message queue: used for inter-process communication only. Faster than socket, but message length and
    queue length are limited. We will design a small algorithm that will prioritize sysv once itc message is not large.

