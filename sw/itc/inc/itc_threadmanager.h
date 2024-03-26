/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

#ifndef __ITC_THREAD_MANAGER_H__
#define __ITC_THREAD_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "itc_impl.h"

/*****************************************************************************\/
*****                                PUBLIC                                *****
*******************************************************************************/
struct itc_threads {
	struct itc_threads*	next;

	void*			(*worker)(void*);
	void*			arg;
	pthread_t		tid;
	bool			use_highest_prio;
	pthread_mutex_t*	start_mtx;
	bool			is_running;
};

struct itc_threadmanager_instance {
	pthread_mutex_t		thrlist_mtx;
	struct itc_threads*	thread_list;
};

extern void set_sched_params(struct result_code* rc, int policy, int selflimit_prio, int priority);
extern void add_itcthread(struct result_code* rc, void* (*worker)(void*), void* arg, bool use_highest_prio, pthread_mutex_t* start_mtx);
extern void start_itcthreads(struct result_code* rc);
extern void terminate_itcthreads(struct result_code* rc);

#ifdef __cplusplus
}
#endif

#endif // __ITC_THREAD_MANAGER_H__