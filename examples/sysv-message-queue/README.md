```bash
$ gcc -Wall -Werror -Wextra -o send sender.c
$ gcc -Wall -Werror -Wextra -o receive receiver.c

$ ./send 1 "Hi Bob!"
$ ./receive 1

$ ./send 2 "How are you, Lucy?"
$ ./receive 2

# See ipc stats, or remove msg in queues 
$ ipcs -qa
$ ipcrm --all=msg
$ ipcrm -q <msqid>

# Create other users for testing
$ sudo adduser <username>
Enter password:

# Test example 1, sending messages over different users by specifying message queue permission is 0666.
bob@172.30.247.32  $ sudo adduser lucy
bob@172.30.247.32  $ ./send 1 "Hi Lucy, I'm Bob. Are you still there?"
lucy@172.30.247.32 $ ./receive 1
lucy@172.30.247.32 $ ./send 2 "Yes, I am. How is it going, Bob?"
bob@172.30.247.32  $ ./receive 2

# Note: you can change magic numbers 1 and 2 in the above commands to macro of LUCY_QUEUEID and BOB_QUEUEID respectively for easier understanding.
```