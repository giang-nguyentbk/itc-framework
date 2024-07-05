```bash
$ gcc -Wall -Werror -Wextra -o send sender.c -lrt
$ gcc -Wall -Werror -Wextra -o receive receiver.c -lrt

$ mkdir -p /tmp/sysvshm_test
$ touch /tmp/sysvshm_test/shared_memory_key
$ touch /tmp/sysvshm_test/sem-mutex-key
$ touch /tmp/sysvshm_test/sem-buffer-count-key
$ touch /tmp/sysvshm_test/sem-spool-signal-key

$ ./receive

$ ./send

```