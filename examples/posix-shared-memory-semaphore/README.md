```bash
$ gcc -Wall -Werror -Wextra -o send sender.c -lrt
$ gcc -Wall -Werror -Wextra -o receive receiver.c -lrt

$ ./receive

$ ./send
Please type a message: abcxyz

$ tail -f /tmp/example.log

```