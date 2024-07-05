all: sender receiver gen

sender: sender.c
	gcc -Wall -Werror -Wextra -o sender sender.c

receiver: receiver.c
	gcc -Wall -Werror -Wextra -o receiver receiver.c

gen:
	mkdir -p /tmp/sysvshm_test
	touch /tmp/sysvshm_test/shared_memory_key
	touch /tmp/sysvshm_test/sem-mutex-key
	touch /tmp/sysvshm_test/sem-buffer-count-key
	touch /tmp/sysvshm_test/sem-spool-signal-key

rmv:
	rm -rf /tmp/sysvshm_test/*

clean:
	rm -rf ./sender ./receiver