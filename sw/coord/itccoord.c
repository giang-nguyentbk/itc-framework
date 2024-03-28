#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <search.h>
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>

#include "itc.h"
#include "itc_impl.h"
#include "itc_queue.h"
#include "itc_proto.h"


/* ITC coordinator will manage a queue of processes that has been active and connected to itccoord via itc_send.
** So, first declare what need to be managed within a process
** 	+ First, each process may have several mailbox, but we don't need to hold all mailbox's details, only general info instead.
** 	So let's create a simple struct itc_mbox_info holding some info such as mailbox id, name,... */

/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
typedef enum {
	PROC_UNUSED = 0,
	PROC_LISTENING, // waiting for connection accepted by itccoord to add the process to the process queue
	PROC_CONNECTED,
	PROC_INVALID
} process_state_e;

struct itc_mbox_info {
	itc_mbox_id_t		mbox_id;
	char			mbox_name[1];
};

struct itc_process {
	struct itc_queue	*mbox_list; // Holding a queue of mailbox inside a process
	itc_mbox_id_t		mbox_id_in_itccoord; // Mailbox id of the process in itccoord, should be only masked with 3 left-most hexes 0xFFF00000
	pid_t			pid; // PID of the process
	int			sockfd; // socket fd of the process
	struct sockaddr_un	sockaddr;
	process_state_e		state;
};

struct itccoord_instance {
	struct itc_queue	*free_list; // Hold a list of process slots that mailbox id (masked with 0xFFF00000) can be assigned to a newly connected process
	struct itc_queue	*used_list; // On the other hand, used list manages processes that were connected and assigned coord's mailbox id.

	itc_mbox_id_t		mbox_id;
	int			mbox_fd;
	int			sockfd;
	uint32_t		freelist_count;

	struct itc_process	processes[MAX_SUPPORTED_PROCESSES];
};



/*****************************************************************************\/
*****                     INTERNAL VARIABLES IN ITC.C                      *****
*******************************************************************************/
static struct itccoord_instance itccoord_inst;
static __thread struct result_code* rc = NULL; // A thread only owns one return code



/* Before main function can be started we first need to create a exit_handler function that suppresses signals that can terminate itccoord process,
** and cleans up our resource , and then raise the terminating signal again */
/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void itccoord_init(void);
static void itccoord_sig_handler(int signo);
static void itccoord_exit_handler(void); // -------------> PENDING
static bool is_itccoord_running(void); // A function to see if itccoord is already running or not
static bool create_itccoord_dir(void); // Used to create and grant permission to /tmp/itc/itccoord/ path
static bool handle_locate_coord_request(int sd); // Handle itc_locate_coord_request from a process
static bool disconnect_process(struct itc_process *proc); // Disconnect from a process that has called lsock_exit or by any reason its socket is closed
static bool close_socket_connection(struct itc_process *proc);
static bool remove_all_mbox_in_process(itc_mbox_id_t mbox_id_in_itccoord);




/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{

	if(rc != NULL)
	{
		rc->flags = ITC_OK;
	} else
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
                	exit(EXIT_FAILURE);
		}	
	}

	if(is_itccoord_running() == false)
	{
		/* itccoord is already running 
		** If it has died unexpectedly, you may need to remove /tmp/itc/itccoord/itc_coordinator and restart it again */
		exit(EXIT_FAILURE);
	}

	if(!create_itccoord_dir() == false)
	{
		exit(EXIT_FAILURE);
	}

	// At normal termination we just clean up our resources by registration a exit_handler
	atexit(itccoord_exit_handler);

	// Allocate two mailboxes, one is for itccoord-self, one is reserved
	if(itc_init(2, ITC_MALLOC, NULL, ITC_FLAGS_I_AM_ITC_COORD) == false)
	{
		exit(EXIT_FAILURE);
	}

	itccoord_inst.mbox_id = itc_create_mailbox(ITC_COORD_NAME, 0);
	if(itccoord_inst.mbox_id == ITC_NO_MBOX_ID)
	{
		exit(EXIT_FAILURE);
	}

	itccoord_inst.mbox_fd = itc_get_fd();

	itccoord_inst.free_list = q_init(rc);
	CHECK_RC_EXIT(rc);

	/* The first process (process 0) is root process which is illegal for us to use, so set it to INVALID state */
	struct itc_process* itc_proc;
	itc_proc 			= &itccoord_inst.processes[0];
	itc_proc->mbox_id_in_itccoord	= ITC_NO_MBOX_ID;
	itc_proc->pid			= -1;
	itc_proc->sockfd		= -1;
	itc_proc->state			= PROC_INVALID;

	/* The second process (process 1) is our itccoord process */
	itc_proc 			= &itccoord_inst.processes[1];
	itc_proc->mbox_id_in_itccoord	= 1 << ITC_COORD_SHIFT;
	itc_proc->pid			= getpid();
	itc_proc->sockfd		= -1;
	itc_proc->state			= PROC_CONNECTED;

	/* Go through all other process's slots and assign init value and enqueue to free list */
	for(int i = 2; i < MAX_SUPPORTED_PROCESSES; i++)
	{
		itc_proc 			= &itccoord_inst.processes[i];
		memset(itc_proc, 0, sizeof(struct itc_process));
		itc_proc->mbox_id_in_itccoord	= (i) << ITC_COORD_SHIFT;
		itc_proc->pid			= -1;
		itc_proc->sockfd		= -1;
		q_enqueue(rc, itccoord_inst.free_list, itc_proc);
		CHECK_RC_EXIT(rc);
		itccoord_inst.freelist_count++;
	}

	itccoord_inst.sockfd = socket(AF_LOCAL, SOCK_STREAM,, 0);
	if(itccoord_inst.sockfd == -1)
	{
		exit(EXIT_FAILURE);
	}

	struct sockaddr_un coord_addr;
	memset(&coord_addr, 0, sizeof(struct sockaddr_un));
	coord_addr.sun_family = AF_LOCAL;
	strcpy(coord_addr.sun_path, ITC_ITCCOORD_FILENAME);

	int res = bind(itccoord_inst.sockfd, (struct sockaddr*)&coord_addr, sizeof(coord_addr));
	if(res < 0)
	{
		exit(EXIT_FAILURE);
	}

	res = chmod(ITC_ITCCOORD_FILENAME, 0777); // Using socket we do not actually need to fopen "/tmp/itc/itccoord/itc_coordinator" by ourselves
	if(res < 0)
	{
		exit(EXIT_FAILURE);
	}

	res = listen(itccoord_inst.sockfd, 10); // Listening and waiting for locating itccoord from other processes, maximum 10 processes will be queued at a time
	if(res < 0)
	{
		exit(EXIT_FAILURE);
	}

	fd_set proc_fd_list;
	int max_fd;
	struct itcq_node *iter;
	struct itc_process *proc;
	while(true)
	{
		/* We use FD_... macros to manage a set of file desciptors that are actually sock fd created by lsock_init function */
		FD_ZERO(&proc_fd_list); // Clear all fd in the set
		FD_SET(itccoord_inst.sockfd, &proc_fd_list); // Add itccoord socket fd
		max_fd = itccoord_inst.sockfd + 1;
		FD_SET(itccoord_inst.mbox_fd, &proc_fd_list);
		if(itccoord_inst.mbox_fd >= max_fd)
		{
			max_fd = itccoord_inst.mbox_fd + 1;
		}

		/* Iterating through the used queue and add sockfd created by processes that have just called lsock_init to the fd set */
		for(iter = itccoord_inst.used_list->head; iter != NULL; iter = iter->next)
		{
			proc = (struct itc_process *)iter->p_data;
			if(proc->sockfd != -1)
			{
				FD_SET(proc->sockfd, &proc_fd_list);
				if(proc->sockfd >= max_fd)
				{
					max_fd = proc->sockfd + 1;
				}
			}
		}

		// Monitor those fd to see if any incoming data on them
		res = select(max_fd, &proc_fd_list, NULL, NULL, NULL);
		if(res < 0)
		{
			exit(EXIT_FAILURE);
		}

		if(FD_ISSET(itccoord_inst.sockfd, &proc_fd_list))
		{
			if(handle_locate_coord_request(itccoord_inst.sockfd) == false)
			{
				exit(EXIT_FAILURE);
			}
		}
	}
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void itccoord_init(void)
{
	/* Ignore SIGPIPE signal, because by any reason, any socket/fd that was connected
	** to this process is corrupted a SIGPIPE will be sent to this process and causes it crash.
	** By ignoring this signal, itccoord can be run as a daemon (run on background) */
	signal(SIGPIPE, SIG_IGN);
	// Call our own exit_handler to release all resources if receiving any of below signals
	signal(SIGSEGV, itccoord_sig_handler);
	signal(SIGILL, itccoord_sig_handler); // When CPU executed an instruction it did not understand
	signal(SIGABRT, itccoord_sig_handler);
	signal(SIGFPE, itccoord_sig_handler); // Reports a fatal arithmetic error, for example divide-by-zero
	signal(SIGTERM, itccoord_sig_handler);
	signal(SIGINT, itccoord_sig_handler);
}

static void itccoord_sig_handler(int signo)
{
	// Call our own exit_handler
	itccoord_exit_handler();

	// After clean up, resume raising the suppressed signal
	signal(signo, SIG_DFL); // Inform kernel does fault exit_handler for this kind of signal
	raise(signo);
}

static bool is_itccoord_running(void)
{
	struct stat s;
	char itccoodinator_path[] = ITC_ITCCOORD_FOLDER;

	if((stat(&itccoodinator_path, &s) == 0) || errno != ENOENT)
	{
		// itccoord is running
		return true;
	}

	return false;
}

static bool create_itccoord_dir(void)
{
	int res = mkdir(ITC_ITCCOORD_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		return false;
	}

	res = chmod(ITC_ITCCOORD_FOLDER, 0777);
	if(res < 0)
	{
		return false;
	}

	return true;
}

static bool handle_locate_coord_request(int sd)
{
	struct itc_locate_coord_reply *lreply;
	struct itc_locate_coord_request *lrequest;
	struct itc_process *tmp;
	struct sockaddr_un addr;
	socklen_t addr_len = sizeof(struct sockaddr_un);
	int tmp_sd, res, len;

	tmp_sd = accept(sd, (struct sockaddr *)&addr, &addr_len); // Create a socket connection on the file descriptor of itccoord to communicate with the process
	if(tmp_sd < 0)
	{
		if(errno == ECONNABORTED)
		{
			/* The process that was just connecting to itccoord is closed by the time we handle the request
			** So just ignore the request */
			return true;
		}

		return false;
	}

	size_t RXLEN = 1024;
	lrequest = (struct itc_locate_coord_request*)malloc(RXLEN); // Allocate a rx buffer to receive the request from the process
	if(lrequest == NULL)
	{
		return false;
	}

	len = recv(tmp_sd, lrequest, RXLEN, 0);
	if(len < (size_t)sizeof(struct itc_locate_coord_request))
	{
		if(errno == EIDRM || errno == ENOENT || errno == EINVAL)
		{
			/* The reason could be that the socket connection of the process was just closed 
			** So ignore the request */
			free(lrequest);
			return true;
		}

		/* Not a big problem here so not return */
		lrequest->my_pid = -1;
	} else if(lrequest->msgno != ITC_LOCATE_COORD_REQUEST)
	{
		/* Received unknown request from the process */
		return false;
	}

	tmp = q_dequeue(rc, itccoord_inst.free_list);
	if(rc->flags != ITC_OK)
	{
		return false;
	}

	if(tmp != NULL)
	{
		itccoord_inst.freelist_count--;
		q_enqueue(rc, itccoord_inst.used_list, tmp);
		if(rc->flags != ITC_OK)
		{
			return false;
		}


		tmp->pid = lrequest->my_pid;

		/* Create a new socket to keep communication with the process later on */
		tmp->sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
		if(tmp->sockfd < 0)
		{
			return false;
		}
		memset(&tmp->sockaddr, 0, sizeof(struct sockaddr_un));
		tmp->sockaddr.sun_family = AF_LOCAL;
		sprintf(tmp->sockaddr.sun_path, "%s%s_0x%08x", ITC_ITCCOORD_FOLDER, ITC_COORD_NAME, tmp->mbox_id_in_itccoord);

		res = bind(tmp->sockfd, (struct sockaddr*)&tmp->sockaddr, sizeof(tmp->sockaddr));
		if(res < 0)
		{
			return false;
		}

		res = chmod(tmp->sockaddr.sun_path, 0777);
		if(res < 0)
		{
			return false;
		}

		res = listen(tmp->sockfd, 1);
		if(res < 0)
		{
			return false;
		}

		tmp->state = PROC_LISTENING;
	}

	/* Send back response to the process */
	lreply = (struct itc_locate_coord_reply*)malloc(sizeof(struct itc_locate_coord_reply));
	if(lreply == NULL)
	{
		return false;
	}

	lreply->msgno = ITC_LOCATE_COORD_REPLY;
	if(tmp == NULL)
	{
		lreply->my_mbox_id_in_itccoord = ITC_NO_MBOX_ID;
	} else
	{
		lreply->my_mbox_id_in_itccoord = tmp->mbox_id_in_itccoord;
	}

	lreply->itccoord_mask = ITC_COORD_MASK;
	lreply->itccoord_mbox_id = itccoord_inst.mbox_id;

	res = send(tmp_sd, lreply, sizeof(struct itc_locate_coord_reply), 0);
	if(res < 0)
	{
		if(errno == EPIPE)
		{
			free(lreply);
			free(lrequest);
			if(tmp != NULL)
			{
				disconnect_process(tmp);
			}

			return false;
		}

		return false;
	}

	free(lreply);
	free(lrequest);

	res = close(tmp_sd);
	if(res < 0)
	{
		return false;
	}

	return true;
}

static bool disconnect_process(struct itc_process *proc)
{
	/* Iterating through the used queue and search for any node that points to proc
	** Then remove it and concatenate the prev and the next of node iter */
	q_remove(rc, itccoord_inst.used_list, proc);
	proc->state = PROC_UNUSED;

	/* Close socket connection is for the corresponding process */
	if(close_socket_connection(proc) == false)
	{
		return false;
	}

	remove_all_mbox_in_process(proc->mbox_id_in_itccoord); // -------------> PENDING


	
}

static bool close_socket_connection(struct itc_process *proc)
{
	int res, tmp_sd;
	char socket_name[256];

	if(proc->sockfd != -1)
	{
		tmp_sd = proc->sockfd;
		proc->sockfd = -1;

		while(1)
		{
			res = close(tmp_sd);
			if(res != 0)
			{
				if(errno == EINTR)
				{
					continue;
				} else if(errno == EBADF)
				{
					/* Socket has been closed already */
					return true;
				} else
				{
					return false;
				}
			} else
			{
				/* Closed successfully */
				break;
			}
		}

		sprintf(socket_name, "%s%s_0x%08x", ITC_ITCCOORD_FOLDER, ITC_COORD_NAME, proc->mbox_id_in_itccoord);
		if(unlink(socket_name) == -1 && errno != ENOENT)
		{
			return false;
		}
	}

	return true;
}

static bool remove_all_mbox_in_process(itc_mbox_id_t mbox_id_in_itccoord)
{

}