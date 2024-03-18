#include <stdlib.h>

#include "itc_threadmanager.h"
#include "itc_impl.h"

/*****************************************************************************\/
*****                               PRIVATE                                *****
*******************************************************************************/
static int m_sched_policy = SCHED_OTHER;
static int m_sched_priority = 0;
static int m_sched_selflimit_prio = 0;

static struct itc_threadmanager_instance thrman_inst;

static void check_sched_params(struct result_code* rc, int policy, int selflimit_prio, int priority);
static void config_itcthread(struct result_code* rc, void* (*worker)(void*), void* arg, pthread_t* t_id, int policy, int priority);


/*****************************************************************************\/
*****                                PUBLIC                                *****
*******************************************************************************/
/*
* Policy: SCHED_OTHER, SCHED_FIFO, SCHED_RR
* Priority: SCHED_OTHER -> 0, SCHED_FIFO/SCHED_RR -> 1-99
* HighestPrio: If SCHED_FIFO/SCHED_RR, should be only maximum of 40
*
* If Policy = SCHED_OTHER, then the following two parameters can be whatever.
* Remember: You MUST have root permission to run with SCHED_FIFO and SCHED_RR.
*/
void set_sched_params(struct result_code* rc, int policy, int selflimit_prio, int priority)
{
	if(policy == SCHED_OTHER)
	{
		m_sched_policy = SCHED_OTHER;
		m_sched_selflimit_prio = sched_get_priority_max(SCHED_OTHER);
		m_sched_priority = sched_get_priority_min(SCHED_OTHER);
	} else
	{
		check_sched_params(rc, policy, selflimit_prio, priority);
		if(rc->flags != ITC_OK)
		{
			return;
		}

		m_sched_policy = policy;
		m_sched_selflimit_prio = selflimit_prio;
		m_sched_priority = (priority > selflimit_prio) ? selflimit_prio : priority;
	}
}

void add_itcthread(struct result_code* rc, void* (*worker)(void*), void* arg, bool use_highest_prio, pthread_mutex_t* start_mtx)
{
	struct itc_threads* thr;

	thr = (struct itc_threads*)malloc(sizeof(struct itc_threads));
	if(thr == NULL)
	{
		rc->flags |= ITC_OUT_OF_MEM;
		return;
	}

	thr->worker = worker;
	thr->arg = arg;
	thr->start_mtx = start_mtx;
	thr->use_highest_prio = use_highest_prio;

	MUTEX_LOCK(rc, &thrman_inst.thrlist_mtx);
	thr->next = thrman_inst.thread_list;
	thrman_inst.thread_list = thr;
	MUTEX_UNLOCK(rc, &thrman_inst.thrlist_mtx);
}

void start_itcthreads(struct result_code* rc)
{
	struct itc_threads* thr;
	struct result_code* rc_tmp;
	
	MUTEX_LOCK(rc, &thrman_inst.thrlist_mtx);
	thr = thrman_inst.thread_list; // Go through the thread lists and try to start them all

	rc_tmp = (struct result_code*)malloc(sizeof(struct result_code));

	while(thr != NULL)
	{
		if(thr->start_mtx != NULL)
		{
			MUTEX_LOCK(rc, thr->start_mtx);
		}

		rc_tmp->flags = ITC_OK;
		config_itcthread(rc_tmp, thr->worker, thr->arg, &thr->tid, m_sched_policy, \
			(thr->use_highest_prio ? m_sched_selflimit_prio : m_sched_priority));
		if(rc_tmp->flags != ITC_OK)
		{
			rc->flags |= rc_tmp->flags;
			break; // Failed to start some thread, stop here.
		}

		if(thr->start_mtx != NULL)
		{
			/* This is a very interesting technique. We take the mutex again to make ourselves go to "sleep".
			* Until the previous MUTEX_LOCK is released by the one who called add_itcthread()
			* and the created thread has finished the initialization, it will release the above lock for us,
			* then we will wake up and release the 2nd lock by below MUTEX_UNLOCK and continue to the next thread. */

			/* Note that: We MUST call MUTEX_UNLOCK for the thread-specific mutex in the "worker" function.
			* Otherwise, we will stuck here indefinitely. */
			MUTEX_LOCK(rc, thr->start_mtx);
			MUTEX_UNLOCK(rc, thr->start_mtx);
		}

		thr = thr->next;
	}

	MUTEX_UNLOCK(rc, &thrman_inst.thrlist_mtx);

	free(rc_tmp);
}

void terminate_itcthreads(struct result_code* rc)
{
	struct itc_threads* thr, *thrtmp;

	MUTEX_LOCK(rc, &thrman_inst.thrlist_mtx);
	thr = thrman_inst.thread_list;
	while(thr != NULL)
	{
		/* To let the created thread trigger thread-specific data destructor, and clean up resources */
		if(pthread_cancel(thr->tid) != 0)
		{
			rc->flags |= ITC_SYSCALL_ERROR;
			break;
		}

		if(pthread_join(thr->tid, NULL) != 0)
		{
			rc->flags |= ITC_SYSCALL_ERROR;
			break;
		}

		thrtmp = thr;
		thr = thr->next;
		free(thrtmp);
	}

	MUTEX_UNLOCK(rc, &thrman_inst.thrlist_mtx);
}

/*****************************************************************************\/
*****                               PRIVATE                                *****
*******************************************************************************/
static void check_sched_params(struct result_code* rc, int policy, int selflimit_prio, int priority)
{
	int max_prio = sched_get_priority_max(policy);
	int min_prio = sched_get_priority_min(policy);

	if(priority < min_prio || priority > max_prio || selflimit_prio < min_prio || selflimit_prio > max_prio)
	{
		rc->flags |= ITC_INVALID_ARGUMENTS;
		return;
	}
}

static void config_itcthread(struct result_code* rc, void* (*worker)(void*), void* arg, pthread_t* t_id, int policy, int priority)
{
	pthread_attr_t t_attr;
	struct sched_param sched_params;

	if(pthread_attr_init(&t_attr) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	if(pthread_attr_setschedpolicy(&t_attr, policy) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	sched_params.sched_priority = priority;
	if(pthread_attr_setschedparam(&t_attr, &sched_params) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	if(pthread_attr_setinheritsched(&t_attr, PTHREAD_EXPLICIT_SCHED) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	if(pthread_create(t_id, &t_attr, worker, arg) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}
}