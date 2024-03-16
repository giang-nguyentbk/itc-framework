#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define IN_THREAD   "\t"
#define START_MAIN  printf("\n-- Main Start -- \n");
#define END_MAIN    printf("-- Main End -- \n\n");  \
                    return 0;
pthread_key_t int_key;
pthread_key_t char_key;
void int_destructor(void * arg) {
	printf(IN_THREAD " int_destructor : %d \n", *(int *)arg);
	free(arg);
}
void char_destructor(void * arg) {
	printf(IN_THREAD " char_destructor : %s \n", (char *)arg);
	free(arg);
}
void * thread_routine(void * arg) {
	(void)arg;
	printf(IN_THREAD "-- Thread Routine Start -- \n");
	printf(IN_THREAD "-- Thread ID:%lu \n", pthread_self());
	int loop;
	int * get_value = NULL;
	int * value = (int *) malloc(sizeof(int));    
	char * get_str = NULL;
	char * str = (char *) malloc(sizeof(char)*20);
	* value = 25;   strcpy(str, "HelloWorld\0");

	pthread_setspecific(int_key, value);
	pthread_setspecific(char_key, str);

	for(loop=0; loop<1000; loop++) {
		printf(IN_THREAD " Loop = %d \n", loop);
		sleep(1);
	}

	get_value   = (int *) pthread_getspecific(int_key);
	get_str     = (char *) pthread_getspecific(char_key);
	printf(IN_THREAD " Value: %d, String: %s \n", *get_value, get_str);
	printf(IN_THREAD "-- Thread Routine End -- \n");
	pthread_exit(NULL);
}

int main() {
	START_MAIN;
	int ret=0;
	pthread_t thread_id;

	ret = pthread_key_create(&int_key, int_destructor);
	if(ret) {
		switch(ret) {
		case EAGAIN:
			printf("The system lacked the necessary resources to create. \n");
			break;
		case ENOMEM:
			printf("Insufficient memory exists to create the key. \n");
			break;
		default:
			printf(" pthread_key_create() failed. Ret=%d \n", ret);
			break;
		}
		exit(0);
	}
	pthread_key_create(&char_key, char_destructor);

	pthread_create(&thread_id, NULL, thread_routine, NULL);
	sleep(4);   // Make sure that the created thread started running 4/10 loops
	pthread_cancel(thread_id);
	printf("For Thread ID %lu, cancellation request Sent \n", thread_id);
	pthread_join(thread_id, NULL);
	pthread_key_delete(int_key);
	pthread_key_delete(char_key);
	END_MAIN;
}