#ifndef __ITC_QUEUE_H__
#define __ITC_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

#include "itc_impl.h"

struct itcq_node {
	struct itcq_node*	next;
	struct itcq_node*	prev;

	void*			p_data;
};

struct itc_queue {
	struct itcq_node*	head;
	struct itcq_node*	tail;
	struct itcq_node*	search;

	uint32_t		size;
	pthread_mutex_t*	q_mtx;
};

extern struct itc_queue* q_init(struct result_code* rc);
extern void q_exit(struct result_code* rc, struct itc_queue* q);
extern void q_enqueue(struct result_code* rc, struct itc_queue*q, void* data);
extern void* q_dequeue(struct result_code* rc, struct itc_queue*q);


#ifdef __cplusplus
}
#endif

#endif // __ITC_QUEUE_H__