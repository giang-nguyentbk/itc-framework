# itc-framework
```bash
How many lines of code in this repo?
$ git ls-files | grep "\(.c\|.sig\|.h\)$" | xargs wc -l
-> Total: 3007 lines (updated 17/3/2024)
-> Total: 5757 lines (updated 24/3/2024)
-> Total: 6582 lines (updated 25/3/2024)
-> Total: 7257 lines (updated 26/3/2024)
-> Total: 8237 lines (updated 27/3/2024)
-> Total: 10164 lines (updated 1/4/2024)
-> Total: 19406 lines (updated 19/4/2024)
-> Total: 22682 lines (updated 20/4/2024)
-> Total: 28381 lines (updated 20/4/2024)
-> Total: 28385 lines (updated 22/4/2024)
-> Total: 28058 lines (updated 3/5/2024)
-> Total: 34335 lines (updated 7/6/2024)
-> Total: 24186 lines (updated 7/6/2024) -> itc-v1.0.1: optimization
-> Total: 25662 lines (updated 23/6/2024)
-> Total: 26940 lines (updated 23/6/2024) -> itc-v2.0.0: posixmq + posixshm
```

## Fundamental knowledge:
### 1. General architecture:

![](./assets/itc-architecture.png?raw=true)
```
This is an implementation that allows exchanging user's messages between threads, processes and even boards.

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

## 4. Sequence Diagram

+ [ITC API](https://www.plantuml.com/plantuml/png/dLPVRzis47_NfxYw3tDK5tatuA6QxNG1ajYpdNN5DX2CT9A9IgHBKawSJp_Ib2bGje9W3qMj-tFt-zsVOxueJQMkgpBwi9Eqg2Xe5A-WPOtHatY1mVgWD5QGo7JBDAQwbZW0f_fTA3s0ZychaK6qCt5OodQKQz1RYJHBdDdjPWekYZ6-mqhmwPRbkU212zCfgIYx92nBaPw9ZOoMPr9rKFlgvxcn4kcF27a61b5NcmeXiwwmU5OdU_z_D9c09PCiRs4oYGefwfqF08mp7VclpxzwpHWih-5LZx9aIYDdl031mU94_GyVNEjgTssCha-6zWSv_cASQY47xmZdnHP1QPkHPvPDr68dHzXDlqmatuUnrrhaZH0-__rHOlPlaD1YFhP3emDe7BZG298LMmqYRuiC0COT52iuBO4fWujdb1iw1uiBh9ldvPC5c9pk5-Dg-12MmIUW7EoCQ8sPb-P2WjwQ33ifKbIgfM0v-aaPmzhElosN4n3vGbmrejQgwpCrUesDnwcstSAcHoFIQFjB7DeDEQsmBNXWxbW2zHa3Az-GE3P3IXuMczkRxsIwMAnc99dDoFrlYx_Yk6TgagoT4Is0mXFZL1v08JMB3reY1jaQDkEJ4s5dW-NMje-sKmyLUyKHLAhWuX9fQsF0bMwwW78dHwLeWJ01cbN6V6ioe8HFJgcAg7NkvFNEFMngpWI7k8dJPQJiToyXg-tduq_M66xp9g42erEqGZ-atghZignrm5mBq5GMg5qakmxo1NxMM6DlZ0UXMNu0YIcoFKfuEi3EUPDKy0oe-OTxv9haMT-sMEn2Rr6U4AFghhetjiiEQxlLBUhGsK2mfLBSNWo__c5gUuYFQUrUg1rD4JvCuE7nxcxK3FhDjncP9Pk4R1R4V7_zjevZ1qfOcYw41sETh94rTMx6FuuRRDxUm-dEG5E-lZ7m1aFDAXIrlkhjnuR-mA2fkHHLNnjzYZU6vus3jNAW84XrgHSotPjUT0Dvp3T2FbCPTDHXT9Xi4ByAL_qKpcOeSbLlM-uXz_JBEmJmPfYA364pNSAEseSKj0XAR6SyRpXObUSvCYelEb52FdetexQYi1LAg-Z_79WCItpFan7Q3Pith_VWp92UBcZZuiUivmcvz8Qixlzy-mrPOINsFeMhY9Puja85QkAtkPzaO3RiUnA3oq1B6-i0T6ymqASIhQr4NKj-yQmiRB-UcS8HA7yRoDVvXZmaz_FrCfdEoMh-n-DylN4dmZd2uspfts0d2a3rn5dUVO_ZosFXARXQXNzdVZ6okYh_0m00)

![](./assets/locating_mbox_procedure.png?raw=true)

+ [Locating Mailbox Procedure](https://www.plantuml.com/plantuml/png/ZLDDRnen4BtxLqpf9QNgeLSagb14FoG8QQ1gSx8Sxu0jhMra3scQNr-lKTmrHjfmGkFtyVncVHr8U3gQbhqvUB4t0WZTCv0_8djmJp3Q_0s41cPUAaqewUXn16-NuX5jW_w4-UO2mSSudZt7OtQ8YbhgWx048S4uAO-YAKuqIHwI-FbOEkShxFvFO54BeY7hkTwGwkAWSNQkz6xdx0KYjwGkcjG7ELHrv56YVintoM5ogEaKbauAqdOFAw7RBkIjTnARM0fCfwpe0ZvywhbDql-sesCSMNc5JuJUYlOT-uywvvpmYTPo4_skIdI45vuJCATSN3UrR6fa0kFnz-sSBzVpsNR1Lv_Nl_dc_cRExnO_VYuss_6OlK1BcXAsQV7b2ZbC0OshkAJ_vMwzud7uzTUcPyDEX4nDMvp0WtUYaI9G54gSlbH5JwIojcvIrwrjNVTbxa3jVLR-nbxVQeAViGVAJOQvfG6xs-LzNKvUJTteEQL_i-5T9ceEMdOv5FBrNx9rXE3rNX6uNObZ47_lOMI5mN0G4aUWTx3ppe0ZXHwK2vIUS2So_8Il8_Hesdy0)

![](./assets/itc_api.png?raw=true)

+ [ITC Gateway](https://www.plantuml.com/plantuml/uml/nLP_Rzis4FrVd-BY04CR8F4e8CLcB44Lsqa3DAjdE-j-e20OwMmJaqYNejkcdtud4ewfM78SBC0C98Pvl7Vl7T-9UbjQOUogoAE3fH7pGe15_HsiMM5qfxz1Pt9VMYmWCUb2Mapjoc073j-BBwWoD7xFErrQO9reIL0obKkXBDm8cLEG9xNLwscVUqJEV2wjm9Mm-5NS0oFav3jLgELqRWSZ_WYzC-sFU3-Ng6nZQ8Bc2vegH3I12Zry1upZpZQ5k1A7XLRzXPpDj6g5Zji9-L2TVxnXqjAFkB5z5zi2piuWP7nsLgtqjLAOMfZWvnMg5AjbrpSuFWT7e0SNHeii5QIymB8KSop1QeXFJq-2lnwhFe2aaGxanp9IsY9e-WaEBmA00Ed68_NSCj6w-WWpemjOQ0h-qgs-EEkUGov9f99g3bgvlJPTzhfTzdDymjxyTC9EtlJYwjDrEgkQKVG3r6l7BZ79CWYFU0FjmYBB39QLCfy35vCfsHdHHmbSpfKs22BFuMwjmmNNNV4Ax4AMJaR0o2DIHyFJxi7HaLFEgt0o6F3HS3W-EWew5svl5B6c_K1otJbGWcWjHUhvMrt3Si82VhNOyV7VXtEAgq_djLezIBrPNhsEGex1bf1ZI3nihszilNwpOZWKdrV2fRyJ-MnTNce5vTgkcLPuqD1fRxeUN1S5Pb9OpE-fg9pFqJp7pr6JavxdudWV5xFJfstCcXmGxxOnUt4RiqSsPi-pSOk8PXl7kstCTjYOjTauVjh6SPEDsHushZyuNzfy__yZz0ejUFja1qzDBmkcztALv_jCRzk5HEjhjJ3ybYw4ccDzmyDR0tK_k1ozEsy-36xV3_aVV_x5ht-T3iUNIN_8amlQD9pUZXWV3tyZkuPPBHxdbny7V91C4pxzm5t79pMVXn1UU2uizPw62K7SqHwGDkN-u3SJIVR-1pCAWLHM-yahgSyYtxIcRkswepSJIFyzFoAwUm973JLfa-TYwqvedv4n7bCwcgL16wPJoEC6SftKae3NarpuCuElv5I4Zi4K9K5tNBoW29UKiCHKpcH5rRVHeuLJ5xAYrcnUWVO_-pqSHXi6iaoRt5adG8yTWzcdnmPASrBvwlMkCnbZGO7M1wEVxkt7Je0Jb0zXcii7yR1y0BTN-H0dk7Z3Is5yy_lsfK21_wQx_KYQtdNVKjwgoFy5)

![](./assets/itc_gateway.png?raw=true)

### 5. Unit Test
+ Thread manager:

![](./assets/thread-man-UT.png?raw=true)

+ Queue:

![](./assets/queue-UT.png?raw=true)


### 6. Daemon and Logging
+ itccoord:

![](./assets/itccoord_log.png?raw=true)

+ itcgws:

![](./assets/itcgws_log.png?raw=true)

### 7. Build static and dynamic library

![](./assets/compilation.png?raw=true)

![](./assets/libitc.png?raw=true)

![](./assets/itccoord_exe.png?raw=true)

![](./assets/itcgws_exe.png?raw=true)

### 8. Performance and latency evaluation
```
1. Thread to Thread (local thread shared memory)
	+ Measured time: ~50 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/local_pe_excel.png?raw=true)

![](./assets/local_pe.png?raw=true)

```
2. Process to Process (sysvmq)
	+ Measured time: ~100 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/sysvmq_pe_excel.png?raw=true)

```
3. Host to Host (local + sysvmq + TCP socket)
	+ Measured time: ~300 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/over_host_sysvmq_excel.png?raw=true)

```
4. Process to Process (posixmq)
	+ Measured time: ~185 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/posixmq_pe_excel.png?raw=true)

```
5. Host to Host (local + posixmq + TCP socket)
	+ Measured time: ~400 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/over_host_posixmq_excel.png?raw=true)

```
6. Process to Process (posixshm)
	+ Measured time: ~100 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/posixshm_pe_excel.png?raw=true)

```
7. Host to Host (local + posixshm + TCP socket)
	+ Measured time: ~200 us (after removing all debugging tracepoints in between of itc_send and itc_receive)
```
![](./assets/over_host_posixshm_excel.png?raw=true)

### 9. Future Improvements
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

6. Evaluate latency in itc message exchanges. Reduce latency down to a specific criteria.

7. Implement all message passing or signal synchronization mechanism in Linux like Shared Memory, Semaphore,...

8. Make this platform portable code, support on Windows, BSD, SunOS,...

----> All improvements will be soon committed in ITC V2.
```

