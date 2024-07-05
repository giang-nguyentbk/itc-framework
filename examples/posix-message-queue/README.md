```bash
$ gcc -Wall -Werror -Wextra -o send sender.c -lrt
$ gcc -Wall -Werror -Wextra -o receive receiver.c -lrt

$ ./receive

$ ./send
Ask for a token (Press <ENTER>): 

# See ipc stats, or remove msg in queues 
$ cat /dev/mqueue/sp-example-server

# Create other users for testing
$ sudo adduser <username>
Enter password:

```