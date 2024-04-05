#include <stdlib.h>
#include <malloc.h>
#include <pthread.h>

#include "itc_queue.h"
#include "itc_impl.h"



/*****************************************************************************\/
*****                           INTERNAL TYPES                             *****
*******************************************************************************/


/*****************************************************************************\/
*****                          INTERNAL VARIABLES                          *****
*******************************************************************************/



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static struct itcq_node* q_createnode(struct result_code* rc, void* data);
static void q_removenode(struct result_code* rc, struct itcq_node** qitem);


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
struct itc_queue* q_init(struct result_code* rc)
{
	struct itc_queue* q;

	q = (struct itc_queue*)malloc(sizeof(struct itc_queue));
	if(q == NULL)
	{
		// Print out a ERROR trace here is needed.
		perror("\tDEBUG: q_init - malloc");
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	q->head = NULL;
	q->tail = NULL;
	q->search = NULL;
	q->size = 0;

	q->q_mtx = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	if(q->q_mtx == NULL)
	{
		free(q);
		perror("\tDEBUG: q_init - malloc - q_mtx");
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	int ret = pthread_mutex_init(q->q_mtx, NULL);
	if(ret != 0)
	{
		free(q->q_mtx);
		free(q);
		printf("\tDEBUG: q_init - pthread_key_delete error code = %d\n", ret);
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	return q;
}

void q_exit(struct result_code* rc, struct itc_queue* q)
{
	void* data;

	if(q == NULL)
	{
		printf("\tDEBUG: q_exit - Queue null!\n");
		rc->flags |= ITC_QUEUE_NULL;
		return;
	}

	if(q->size > 0)
	{
		printf("\tDEBUG: q_exit - Queue still has items, removing them!\n");
		// Go through and discard all data pointed to by all nodes in the queue
		while((data = q_dequeue(rc, q)) != NULL)
		{
			// Queue is not allowed to free() user data by itself, just free() the pointer to user data on each node.
			// free(node->p_data);
		}
	}

	rc->flags &= ~ITC_QUEUE_EMPTY; // After discard all nodes, it automatically returns a queue_empty error code but it should not, so ignore

	if(q->q_mtx != NULL)
	{
		int ret = pthread_mutex_destroy(q->q_mtx);
		if(ret != 0)
		{
			printf("\tDEBUG: q_exit - pthread_mutex_destroy error code = %d\n", ret);
			rc->flags |= ITC_SYSCALL_ERROR;
		}
		free(q->q_mtx);
	}

	free(q);
}

void q_enqueue(struct result_code* rc, struct itc_queue*q, void* data)
{
	struct itcq_node* new_node;

	new_node = q_createnode(rc, data);

	if(rc->flags != ITC_OK)
	{
		// q_createnode() failed due to out of memory
		printf("\tDEBUG: q_enqueue - q_createnode failed, rc = %d!\n", rc->flags);
		return;
	}

	MUTEX_LOCK(q->q_mtx);

	// Check if the queue tail is NULL or not.
	// If yes, so the queue now is empty, so move q->head and q->tail to the 1st item.
	// If not, just update the last item to point to new item and move q->tail to new item as well. 
	if(q->tail == NULL)
	{
		printf("\tDEBUG: q_enqueue - Queue now is empty, add the first node!\n");
		q->head = new_node;
		q->tail = new_node;
	} else
	{
		q->tail->next = new_node;
		new_node->prev = q->tail;
		q->tail = new_node;
	}

	q->size++;
	MUTEX_UNLOCK(q->q_mtx);
}

void* q_dequeue(struct result_code* rc, struct itc_queue*q)
{
	void* data;

	MUTEX_LOCK(q->q_mtx);

	// Queue empty
	if(q->head == NULL)
	{
		printf("\tDEBUG: q_dequeue - Queue empty!\n");
		rc->flags |= ITC_QUEUE_EMPTY;
		MUTEX_UNLOCK(q->q_mtx);
		return NULL;
	}

	data = q->head->p_data;

	// In case queue has only one item
	if(q->head == q->tail)
	{
		printf("\tDEBUG: q_dequeue - Queue has only one item!\n");
		q_removenode(rc, &q->head);
		// Both head and tail should be moved to NULL
		q->head = NULL;
		q->tail = NULL;

		// Sanity check
		if(q->size > 1)
		{
			printf("\tDEBUG: q_dequeue - Queue got messed up, q->size = %d!\n", q->size);
		}
	} else
	{
		// In case queue has more than one items, move head to the 2nd item and remove the 1st item via prev
		// pointer of the 2nd.
		if(q->size > 25)
		{
			if((q->size % 25) == 0)
			{
				printf("\tDEBUG: q_dequeue - Queue currently has %d items!\n", q->size);
			}
		} else
		{
			printf("\tDEBUG: q_dequeue - Queue currently has %d items!\n", q->size);
		}

		q->head = q->head->next;
		q_removenode(rc, &q->head->prev);
	}

	q->size--;
	MUTEX_UNLOCK(q->q_mtx);

	return data;
}

void q_remove(struct result_code* rc, struct itc_queue* q, void* data)
{
	/* Functionality is same as q_dequeue but you can dequeue a node at any position instead */

	struct itcq_node *iter = NULL;

	/* Iterating through the used queue and search for any node that points to proc
	** Then remove it and concatenate the prev and the next of node iter */
	for(iter = q->head; iter != NULL; iter = iter->next)
	{
		if(iter->p_data == data)
		{
			break;
		}
	}

	/* Found a node from queue */
	if(iter != NULL)
	{
		/* Found node is the first one */
		if(iter == q->head)
		{
			/* Queue has only one node */
			if(q->size == 1)
			{
				q->head = NULL;
				q->tail = NULL;
			} else
			{
				q->head = iter->next;
				q->head->prev = NULL;
			}
		} else if(iter == q->tail) /* Found node is the last one, and queue must have more than two nodes at here */
		{
			q->tail = iter->prev;
			q->tail->next = NULL;
		} else
		{
			/* Found node is in the middle of more-than-two-node queue */
			iter->prev->next = iter->next;
			iter->next->prev = iter->prev;
		}
	} else
	{
		/* Not found so nothing to be removed */
		return;
	}

	q->size--;
	q_removenode(rc, &iter);
}

void q_clear(struct result_code* rc, struct itc_queue* q)
{
	void* data;

	if(q == NULL)
	{
		printf("\tDEBUG: q_clear - Queue null!\n");
		rc->flags |= ITC_QUEUE_NULL;
		return;
	}

	if(q->size > 0)
	{
		printf("\tDEBUG: q_clear - Queue still has items, removing them!\n");
		// Go through and discard all data pointed to by all nodes in the queue
		while((data = q_dequeue(rc, q)) != NULL)
		{
			// Queue is not allowed to free() user data by itself, just free() the pointer to user data on each node.
			// free(node->p_data);
		}
	}
}

/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static struct itcq_node* q_createnode(struct result_code* rc, void* data)
{
	struct itcq_node* retnode;

	retnode = (struct itcq_node*)malloc(sizeof(struct itcq_node));
	if(retnode == NULL)
	{
		// Print out an ERROR trace here is needed.
		perror("\tDEBUG: q_createnode - malloc");
		rc->flags |= ITC_SYSCALL_ERROR;
		return NULL;
	}

	retnode->p_data = data;
	retnode->next = NULL;
	retnode->prev = NULL;

	return retnode;
}

static void q_removenode(struct result_code* rc, struct itcq_node** node)
{
	if(node == NULL || *node == NULL)
	{
		rc->flags |= ITC_FREE_NULL_PTR;
		return;
	}

	free(*node);
	*node = NULL;
}