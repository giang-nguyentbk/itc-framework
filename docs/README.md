# Inter-communication in Unix-like system

![](../assets/inter-connect.png?raw=true)

## SYSV Message Queue
+ Max message size: 8192 bytes (8KB)
+ Max number of msg queues: 32000 queues
+ Max number of msg in a queue: 16384 bytes (16KB)


## [POSIX Message Queue](https://man7.org/tlpi/download/TLPI-52-POSIX_Message_Queues.pdf)
+ Max message size: 8KB (< Linux 2.6.28) -> 1MB (Linux 2.6.28 to Linux 3.4) -> 16MB (> Linux 3.5)
+ Max number of msg queues: 256 queues
+ Max number of msg in a queu: 65,536 (> Linux 3.5)