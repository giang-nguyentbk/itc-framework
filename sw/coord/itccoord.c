#define _GNU_SOURCE
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


/* ITC coordinator will manage a queue of processes that has been active and connected to itccoord via lsock_locate_coord.
** So, first declare what need to be managed within a process
** 	+ First, each process may have several mailbox, but we don't need to hold all mailbox's details, only general info instead.
** 	So let's create a simple struct itc_mbox_info holding some info such as mailbox id, name,... */

/* Explanation:
** 1. To be simple, itccoord in first version only manages free_list and used_list of processes that are running with ITC system.
** 2. At ITC initialization of each process, they must locate itccoord firstly.
** 3. Itccoord will run a while true loop in order to:
** 	+ After seeing something is coming to itccoord by select(), itcoord will call handle_locate_coord_request() to dequeue a process from free_list, add to used_list and open a socket connection ready for connect() from lsock_init.
** 	+ Next, it will check all processes in used_list to if any process want to release or any new process which has just been found by select().
**	+ If process state is LISTENING -> it's been locating itccoord -> connect to it.
**	+ If process state is CONNECTED -> it want to release (process is gonna be killed) -> disconnect from it.
**	+ There are some cases where a process in used_list (added after locating but killed during lsock_init) -> cause it's to be zombie (still in used_list but not exist anymore and will never talk/connect to itccoord to change its state to CONNECTED).
**	+ So, in that case, if after 10 while loop or free_list_count <= a threshold, we still see the process state stays at LISTENING, we need to validate the process aliveness */

/* Sequence:
itccoord	:	itccoord_inst.sockfd = socket(AF_LOCAL, SOCK_STREAM,, 0)
itccoord	:	bind(itccoord_inst.sockfd, (struct sockaddr*)&coord_addr, sizeof(coord_addr)) ---> /tmp/itc/itccoord/itc_coordinator
itccoord	:	listen(itccoord_inst.sockfd, 10)

process (lsock)	: 	sd = socket(AF_LOCAL, SOCK_STREAM, 0)
process (lsock)	:	res = connect(sd, (struct sockaddr*)&coord_addr, sizeof(coord_addr)) ---> /tmp/itc/itccoord/itc_coordinator
process (lsock)	:	res = send(sd, lrequest, sizeof(struct itc_locate_coord_request), 0)

itccoord	:	handle_locate_coord_request()
itccoord	:	tmp_sd = accept(sd, (struct sockaddr *)&addr, &addr_len)
itccoord	:	len = recv(tmp_sd, lrequest, RXLEN, 0)
itccoord	:	tmp->sockfd = socket(AF_LOCAL, SOCK_STREAM, 0) 
itccoord	:	res = bind(tmp->sockfd, (struct sockaddr*)&tmp->sockaddr, sizeof(tmp->sockaddr)) ---> /tmp/itc/socket/lsocket_0x...
itccoord	:	res = listen(tmp->sockfd, 1)

process (lsock)	:	rx_len = recv(sd, lreply, RXBUF_LEN, 0) ---> itc_locate_coord_reply
process (lsock)	:	close(sd) ---> LOCATING ITCCOORD DONE!

process (lsock)	:	sd = socket(AF_LOCAL, SOCK_STREAM, 0) ---> lsock_init()
process (lsock)	:	res = connect(sd, (struct sockaddr*)&coord_addr, sizeof(struct sockaddr)) ---> /tmp/itc/socket/lsocket_0x...

itccoord	:	connect_to_process()
itccoord	:	tmp_sd = accept(proc->sockfd, (struct sockaddr *)addr, &addr_len)
itccoord	:	res = send(tmp_sd, "ack", 4, 0) ---> /tmp/itc/socket/lsocket_0x...

process (lsock)	:	rx_len = recv(sd, str_ack, 4, 0) ---> CONNECTING TO A PROCESS DONE FROM ITCCOORD SIDE!
*/

/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
#define NR_PROC_ALIVENESS_CHECK_RETRIES	10 // After 10 while true loop of itccoord main function, check zombie processes.
#define FREELIST_LOW_THRESHOLD		10 // When number of processes remaining in free_list decreases down to 10 -> used_list count should be 255 - 10 = 245, check zombie processes.

union itc_msg {
	uint32_t					msgno;

	struct itc_locate_coord_request			itc_locate_coord_request;
	struct itc_locate_coord_reply			itc_locate_coord_reply;
	struct itc_notify_coord_add_rmv_mbox		itc_notify_coord_add_rmv_mbox;
	struct itc_locate_mbox_sync_request		itc_locate_mbox_sync_request;
	struct itc_locate_mbox_sync_reply		itc_locate_mbox_sync_reply;
	struct itc_locate_mbox_from_itcgws_request	itc_locate_mbox_from_itcgws_request;
	struct itc_locate_mbox_from_itcgws_reply	itc_locate_mbox_from_itcgws_reply;
};

typedef enum {
	PROC_UNUSED = 0,
	PROC_LISTENING, // After a process called lsock_locate_coord, state is changed to LISTENING so that waiting for connect from the process by lsock_init
	PROC_CONNECTED, // After a process called lsock_init
	PROC_INVALID
} process_state_e;

struct itc_mbox_info {
	itc_mbox_id_t		mbox_id;
	char			mbox_name[1];
};

struct itc_process {
	itc_mbox_id_t		mbox_id_in_itccoord; // Mailbox id of the process in itccoord, should be only masked with 3 left-most hexes 0xFFF00000
	pid_t			pid; // PID of the process
	int			sockfd; // socket fd of the process
	struct sockaddr_un	sockaddr;
	process_state_e		state;
};

struct itccoord_instance {
	struct itc_queue	*free_list; // Hold a list of process slots that mailbox id (masked with 0xFFF00000) can be assigned to a newly connected process
	struct itc_queue	*used_list; // On the other hand, used list manages processes that were connected and assigned coord's mailbox id.

	void			*mbox_tree; // Manage all registered mailboxes in itccoord by tsearch, tfind, tdelete,... APIs

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
static void itccoord_exit_handler(void);
static bool is_itccoord_running(void); // A function to see if itccoord is already running or not
static bool create_itccoord_dir(void); // Used to create and grant permission to /tmp/itc/itccoord/ path
static bool handle_locate_coord_request(int sd); // Handle itc_locate_coord_request from a process
static bool disconnect_from_process(struct itc_process *proc); // Disconnect from a process that has called lsock_exit or by any reason its socket is closed
static bool close_socket_connection(struct itc_process *proc);
// static bool remove_all_mbox_in_process(itc_mbox_id_t mbox_id_in_itccoord);
static struct itc_process *find_process(itc_mbox_id_t mbox_id);
static bool connect_to_process(struct itc_process *proc);
static void handle_incoming_request(void);
static void handle_add_mbox(itc_mbox_id_t mbox_id, char *mbox_name);
static void handle_remove_mbox(itc_mbox_id_t mbox_id, char *mbox_name);
static void handle_locate_mbox(itc_mbox_id_t from_mbox, int32_t timeout, bool find_only_internal, char *mbox_name);
static int mbox_name_cmpfunc(const void *pa, const void *pb); // char *mbox_name vs struct itc_mbox_info *mbox2
static int mbox_name_cmpfunc2(const void *pa, const void *pb); // struct itc_mbox_info *mbox1 vs struct itc_mbox_info *mbox2


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	itccoord_init();

	if(rc == NULL)
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
			printf("\tDEBUG: itccoord - Failed to malloc rc, OOM!\n");
                	exit(EXIT_FAILURE);
		}	
	}

	rc->flags = ITC_OK;

	if(is_itccoord_running() == true)
	{
		/* itccoord is already running 
		** If it has died unexpectedly, you may need to remove /tmp/itc/itccoord/itc_coordinator and restart it again */
		printf("\tDEBUG: itccoord - ITCCOORD already running, no need to start again!\n");
		exit(EXIT_FAILURE);
	}

	if(create_itccoord_dir() == false)
	{
		printf("\tDEBUG: itccoord - Failed to create_itccoord_dir()!\n");
		exit(EXIT_FAILURE);
	}

	// At normal termination we just clean up our resources by registration a exit_handler
	atexit(itccoord_exit_handler);

	// Allocate two mailboxes, one is for itccoord-self, one is reserved
	if(itc_init(2, ITC_MALLOC, ITC_FLAGS_I_AM_ITC_COORD) == false)
	{
		printf("\tDEBUG: itccoord - Failed to itc_init(), ITC_FLAGS_I_AM_ITC_COORD!\n");
		exit(EXIT_FAILURE);
	}

	itccoord_inst.mbox_id = itc_create_mailbox(ITC_COORD_MBOX_NAME, 0);
	if(itccoord_inst.mbox_id == ITC_NO_MBOX_ID)
	{
		printf("\tDEBUG: itccoord - Failed to itc_create_mailbox(), mbox_name = %s!\n", ITC_COORD_MBOX_NAME);
		exit(EXIT_FAILURE);
	}

	itccoord_inst.mbox_fd = itc_get_fd(itccoord_inst.mbox_id);

	itccoord_inst.free_list = q_init(rc);
	CHECK_RC_EXIT(rc);
	itccoord_inst.used_list = q_init(rc);
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
	int i = 2;
	printf("\tDEBUG: itccoord - Enqueue process index = %d to free_list!\n", i);
	printf("\tDEBUG: itccoord - Enqueue process n-th to free_list!\n");
	for(; i < MAX_SUPPORTED_PROCESSES; i++)
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
	printf("\tDEBUG: itccoord - Enqueue process index = %d to free_list!\n", i - 1);

	itccoord_inst.sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(itccoord_inst.sockfd == -1)
	{
		printf("\tDEBUG: itccoord - Failed to socket(), errno = %d!\n", errno);
		exit(EXIT_FAILURE);
	}

	struct sockaddr_un coord_addr;
	memset(&coord_addr, 0, sizeof(struct sockaddr_un));
	coord_addr.sun_family = AF_LOCAL;
	strcpy(coord_addr.sun_path, ITC_ITCCOORD_FILENAME);

	// When using bind() remember must use sizeof(coord_addr) here.
	// If you use sizeof(struct sockaddr) instead, there will be a bug that creates a another wrong folder /tmp/itc/socke/, Idk why!!!
	int res = bind(itccoord_inst.sockfd, (struct sockaddr*)&coord_addr, sizeof(coord_addr));
	if(res < 0)
	{
		printf("\tDEBUG: itccoord - Failed to bind(), errno = %d!\n", errno);
		exit(EXIT_FAILURE);
	}

	res = chmod(ITC_ITCCOORD_FILENAME, 0777); // Using socket we do not actually need to fopen "/tmp/itc/itccoord/itc_coordinator" by ourselves
	if(res < 0)
	{
		printf("\tDEBUG: itccoord - Failed to chmod(), errno = %d!\n", errno);
		exit(EXIT_FAILURE);
	}

	res = listen(itccoord_inst.sockfd, 10); // Listening and waiting for locating itccoord from other processes, maximum 10 processes will be queued at a time
	if(res < 0)
	{
		printf("\tDEBUG: itccoord - Failed to listen(), errno = %d!\n", errno);
		exit(EXIT_FAILURE);
	}

	fd_set proc_fd_list;
	int max_fd;
	struct itcq_node *iter;
	struct itc_process *proc;
	int zc_counter = 0; // Zombie process check
	while(true)
	{
		/* We use FD_... macros to manage a set of file desciptors that are actually sock fd created by lsock_init function */
		FD_ZERO(&proc_fd_list); // Clear all fd in the set
		FD_SET(itccoord_inst.sockfd, &proc_fd_list); // Add itccoord socket fd
		max_fd = itccoord_inst.sockfd + 1;
		FD_SET(itccoord_inst.mbox_fd, &proc_fd_list); // Add itccoord mailbox fd
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
			printf("\tDEBUG: itccoord - Failed to select(), errno = %d!\n", errno);
			exit(EXIT_FAILURE);
		} else
		{
			if(FD_ISSET(itccoord_inst.sockfd, &proc_fd_list))
			{
				if(handle_locate_coord_request(itccoord_inst.sockfd) == false)
				{
					printf("\tDEBUG: itccoord - Failed to handle_locate_coord_request()!\n");
					exit(EXIT_FAILURE);
				}
			}

			for(iter = itccoord_inst.used_list->head; iter != NULL; iter = iter->next)
			{
				proc = (struct itc_process *)iter->p_data;
				if(proc->sockfd != -1 && FD_ISSET(proc->sockfd, &proc_fd_list))
				{
					if(proc->state == PROC_LISTENING)
					{
						printf("\tDEBUG: itccoord - PROC_LISTENING!\n");
						if(connect_to_process(proc) == false)
						{
							exit(EXIT_FAILURE);
						}
					} else if(proc->state == PROC_CONNECTED)
					{
						printf("\tDEBUG: itccoord - PROC_CONNECTED!\n");
						if(disconnect_from_process(proc) == false)
						{
							exit(EXIT_FAILURE);
						}
						break;
					} else
					{
						printf("\tDEBUG: itccoord - Wrong process state = %d!\n", proc->state);
						exit(EXIT_FAILURE);
					}
				} else if((zc_counter == (NR_PROC_ALIVENESS_CHECK_RETRIES - 1) || itccoord_inst.freelist_count < FREELIST_LOW_THRESHOLD) && \
					proc->state == PROC_LISTENING && proc->pid > 0)
				{
					/* Try killing the process but received errno no such process */
					if(kill(proc->pid, 0) == -1 && errno == ESRCH)
					{
						printf("\tDEBUG: itccoord - Zomebie process detected!\n");
						disconnect_from_process(proc);
						zc_counter--;
						break;
					}
				}
			}

			if(FD_ISSET(itccoord_inst.mbox_fd, &proc_fd_list))
			{
				printf("\tDEBUG: itccoord - Calling handle_incoming_request()!\n");
				handle_incoming_request(); // Such as ADD, RMV mailboxes,...
			}

			zc_counter++;
			zc_counter %= NR_PROC_ALIVENESS_CHECK_RETRIES; // Keep it in range of 0-9
		}
	}

	exit(EXIT_SUCCESS);
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

static void itccoord_exit_handler(void)
{
	printf("\tDEBUG: itccoord_exit_handler - ITCCOORD is terminated, calling exit handler...\n");
	struct itc_process *proc;

	printf("\tDEBUG: itccoord_exit_handler - Removing all sockets on all active processes...\n");
	for(int i = 0; i < MAX_SUPPORTED_PROCESSES; i++)
	{
		/* Close socket connection for all processes */
		proc = find_process(i << ITC_COORD_SHIFT);
		if(close_socket_connection(proc) == false)
		{
			printf("\tDEBUG: itccoord_exit_handler - Failed to close socket for pid = %d\n", proc->pid);
			return;
		}
	}


	unlink(ITC_ITCCOORD_FILENAME);
	rmdir(ITC_SOCKET_FOLDER);
	rmdir(ITC_ITCCOORD_FOLDER);
	remove(ITC_SYSVMSQ_FILENAME);
	rmdir(ITC_SYSVMSQ_FOLDER);
	rmdir(ITC_BASE_PATH);
	printf("\tDEBUG: itccoord_exit_handler - Remove all directories successfully!\n");

	tdestroy(itccoord_inst.mbox_tree, free); // Call free() on each node's user data
	printf("\tDEBUG: itccoord_exit_handler - Remove all nodes from mbox_tree successfully!\n");

	printf("\tDEBUG: itccoord_exit_handler - Removing processes in free_list, count = %u!\n", itccoord_inst.freelist_count);
	q_exit(rc, itccoord_inst.free_list);
	printf("\tDEBUG: itccoord_exit_handler - Removing processes in used_list, count = %u!\n", 253 - itccoord_inst.freelist_count);
	q_exit(rc, itccoord_inst.used_list);

	itc_delete_mailbox(itccoord_inst.mbox_id);
	itc_exit();

	free(rc);
}

static bool is_itccoord_running(void)
{
	struct stat s;
	char itccoodinator_path[] = ITC_ITCCOORD_FOLDER;

	if((stat(itccoodinator_path, &s) == 0) || errno != ENOENT)
	{
		// itccoord is running
		printf("\tDEBUG: is_itccoord_running - ITCCOORD already running!\n");
		return true;
	}

	return false;
}

static bool create_itccoord_dir(void)
{
	int res;

	res = mkdir(ITC_BASE_PATH, 0777);

	if(res < 0 && errno != EEXIST)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to mkdir %s, errno = %d!\n", ITC_BASE_PATH, errno);
		return false;
	}

	res = chmod(ITC_BASE_PATH, 0777);
	if(res < 0)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to chmod %s, errno = %d!\n", ITC_BASE_PATH, errno);
		return false;
	}

	res = mkdir(ITC_SOCKET_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to mkdir %s, errno = %d!\n", ITC_SOCKET_FOLDER, errno);
		return false;
	}

	res = chmod(ITC_SOCKET_FOLDER, 0777);
	if(res < 0)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to chmod %s, errno = %d!\n", ITC_SOCKET_FOLDER, errno);
		return false;
	}

	res = mkdir(ITC_ITCCOORD_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to mkdir %s, errno = %d!\n", ITC_ITCCOORD_FOLDER, errno);
		return false;
	}

	res = chmod(ITC_ITCCOORD_FOLDER, 0777);
	if(res < 0)
	{
		printf("\tDEBUG: create_itccoord_dir - Failed to chmod %s, errno = %d!\n", ITC_ITCCOORD_FOLDER, errno);
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
	int tmp_sd, res;
	long unsigned int len;

	tmp_sd = accept(sd, (struct sockaddr *)&addr, &addr_len); // Create a socket connection on the file descriptor of itccoord to communicate with the process
	if(tmp_sd < 0)
	{
		if(errno == ECONNABORTED)
		{
			/* The process that was just connecting to itccoord is closed by the time we handle the request
			** So just ignore the request */
			printf("\tDEBUG: handle_locate_coord_request - ECONNABORTED, OK!\n");
			return true;
		}

		printf("\tDEBUG: handle_locate_coord_request - Failed to accept socket connection on sd = %d!\n", sd);
		return false;
	}

	size_t RXLEN = 1024;
	lrequest = (struct itc_locate_coord_request*)malloc(RXLEN); // Allocate a rx buffer to receive the request from the process
	if(lrequest == NULL)
	{
		printf("\tDEBUG: handle_locate_coord_request - Failed to malloc itc_locate_coord_request, OOM!\n");
		return false;
	}

	len = recv(tmp_sd, lrequest, RXLEN, 0);
	if(len < (size_t)sizeof(struct itc_locate_coord_request))
	{
		if(errno == EIDRM || errno == ENOENT || errno == EINVAL)
		{
			/* The reason could be that the socket connection of the process was just closed 
			** So ignore the request */
			printf("\tDEBUG: handle_locate_coord_request - The process's socket has just closed, OK!\n");
			free(lrequest);
			return true;
		}

		/* Not a big problem here so not return */
		lrequest->my_pid = -1;
	} else if(lrequest->msgno != ITC_LOCATE_COORD_REQUEST)
	{
		/* Received unknown request from the process */
		printf("\tDEBUG: handle_locate_coord_request - Unknown message received, expected ITC_LOCATE_COORD_REQUEST!\n");
		return false;
	}

	printf("\tDEBUG: handle_locate_coord_request - Dequeue a process from free_list!\n");
	tmp = q_dequeue(rc, itccoord_inst.free_list);
	if(rc->flags != ITC_OK)
	{
		printf("\tDEBUG: handle_locate_coord_request - Failed to dequeue a process from free_list, rc = %u!\n", rc->flags);
		return false;
	}

	if(tmp != NULL)
	{
		itccoord_inst.freelist_count--;
		printf("\tDEBUG: handle_locate_coord_request - Enqueue process mbox_id = 0x%08x to used_list!\n", tmp->mbox_id_in_itccoord);
		q_enqueue(rc, itccoord_inst.used_list, tmp);
		if(rc->flags != ITC_OK)
		{
			printf("\tDEBUG: handle_locate_coord_request - Failed to enqueue a process to used_list, rc = %u!\n", rc->flags);
			return false;
		}


		tmp->pid = lrequest->my_pid;

		/* Create a new socket to keep communication with the process later on */
		tmp->sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
		if(tmp->sockfd < 0)
		{
			printf("\tDEBUG: handle_locate_coord_request - Failed to create socket, errno = %d!\n", errno);
			return false;
		}
		memset(&tmp->sockaddr, 0, sizeof(struct sockaddr_un));
		tmp->sockaddr.sun_family = AF_LOCAL;
		sprintf(tmp->sockaddr.sun_path, "%s_0x%08x", ITC_LSOCKET_FILENAME, tmp->mbox_id_in_itccoord);

		res = bind(tmp->sockfd, (struct sockaddr*)&tmp->sockaddr, sizeof(tmp->sockaddr));
		if(res < 0)
		{
			printf("\tDEBUG: handle_locate_coord_request - Failed to bind socket, errno = %d!\n", errno);
			return false;
		}

		res = chmod(tmp->sockaddr.sun_path, 0777);
		if(res < 0)
		{
			printf("\tDEBUG: handle_locate_coord_request - Failed to chmod %s, errno = %d!\n", tmp->sockaddr.sun_path, errno);
			return false;
		}

		res = listen(tmp->sockfd, 1);
		if(res < 0)
		{
			printf("\tDEBUG: handle_locate_coord_request - Failed to listen, errno = %d!\n", errno);
			return false;
		}

		tmp->state = PROC_LISTENING;
	}

	/* Send back response to the process */
	lreply = (struct itc_locate_coord_reply*)malloc(sizeof(struct itc_locate_coord_reply));
	if(lreply == NULL)
	{
		printf("\tDEBUG: handle_locate_coord_request - Failed to malloc lreply, OOM!\n");
		return false;
	}

	lreply->msgno = ITC_LOCATE_COORD_REPLY;
	if(tmp == NULL)
	{
		printf("\tDEBUG: handle_locate_coord_request - No more process to allocate for this ITC_LOCATE_COORD_REQUEST!\n");
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
			printf("\tDEBUG: handle_locate_coord_request - Failed to send itc_locate_coord_reply to process pid = %d due to EPIPE!\n", tmp->pid);
			free(lreply);
			free(lrequest);
			if(tmp != NULL)
			{
				disconnect_from_process(tmp);
			}

			printf("\tDEBUG: handle_locate_coord_request - Failed to send itc_locate_coord_reply, EPIPE!\n");
			return false;
		}

		printf("\tDEBUG: handle_locate_coord_request - Failed to send itc_locate_coord_reply, res < 0!\n");
		return false;
	}

	printf("\tDEBUG: handle_locate_coord_request - Sent itc_locate_coord_reply successfully!\n");

	free(lreply);
	free(lrequest);

	res = close(tmp_sd);
	if(res < 0)
	{
		printf("\tDEBUG: handle_locate_coord_request - Failed to close, res = %d, errno = %d!\n", res, errno);
		return false;
	}

	return true;
}

static bool disconnect_from_process(struct itc_process *proc)
{
	/* Iterating through the used queue and search for any node that points to proc
	** Then remove it and concatenate the prev and the next of node iter */
	printf("\tDEBUG: disconnect_from_process - Remove process mbox_id = 0x%08x from used_list!\n", proc->mbox_id_in_itccoord);
	q_remove(rc, itccoord_inst.used_list, proc);
	proc->state = PROC_UNUSED;

	/* Close socket connection is for the corresponding process */
	if(close_socket_connection(proc) == false)
	{
		printf("\tDEBUG: disconnect_from_process - Failed to close socket connection for process mbox_id = 0x%08x\n", proc->mbox_id_in_itccoord);
		return false;
	}

	// remove_all_mbox_in_process(proc->mbox_id_in_itccoord);

	memset(&proc->sockaddr, 0, sizeof(struct sockaddr_un));
	printf("\tDEBUG: disconnect_from_process - Enqueue process mbox_id = 0x%08x to free_list!\n", proc->mbox_id_in_itccoord);
	q_enqueue(rc, itccoord_inst.free_list, proc);
	itccoord_inst.freelist_count++;
	return true;
}

static bool close_socket_connection(struct itc_process *proc)
{
	int res, tmp_sd;
	char socket_name[256];

	if(proc->sockfd != -1)
	{
		tmp_sd = proc->sockfd;
		proc->sockfd = -1;

		printf("\tDEBUG: close_socket_connection - Remove socket on process pid = %d\n", proc->pid);
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
					printf("\tDEBUG: close_socket_connection - errno = EBADF, OK!\n");
					return true;
				} else
				{
					printf("\tDEBUG: close_socket_connection - errno = %d, NOK!\n", errno);
					return false;
				}
			} else
			{
				/* Closed successfully */
				break;
			}
		}

		sprintf(socket_name, "%s_0x%08x", ITC_LSOCKET_FILENAME, proc->mbox_id_in_itccoord);
		if(unlink(socket_name) == -1)
		{
			if(errno != ENOENT)
			{
				printf("\tDEBUG: close_socket_connection - Failed to unlink socket address, errno = %d!\n", errno);
				return false;
			} else
			{
				printf("\tDEBUG: close_socket_connection - No such file or directory, not unlink socket address!\n");
			}
		} else
		{
			printf("\tDEBUG: close_socket_connection - Unlink successfully!\n");
		}
	}

	return true;
}

// static bool remove_all_mbox_in_process(itc_mbox_id_t mbox_id_in_itccoord)
// {
// 	struct itc_mbox_info *mbox;
// 	struct itc_process *proc;

// 	proc = find_process(mbox_id_in_itccoord);
// 	if(proc == NULL)
// 	{
// 		return false;
// 	}

// 	mbox = 
// }

static struct itc_process *find_process(itc_mbox_id_t mbox_id)
{
	int index;

	index = ((mbox_id & ITC_COORD_MASK) >> ITC_COORD_SHIFT);

	if(index > MAX_SUPPORTED_PROCESSES)
	{
		printf("\tDEBUG: find_process - Invalid mbox_id = 0x%08x > MAX_SUPPORTED_PROCESSES = 255!\n", mbox_id);
		return NULL;
	}

	return &itccoord_inst.processes[index];
}

static bool connect_to_process(struct itc_process *proc)
{
	struct sockaddr_un addr;
	socklen_t addr_len = sizeof(struct sockaddr_un);
	int tmp_sd, res;

	printf("\tDEBUG: connect_to_process - Connecting to the process with pid = %d\n", proc->pid);

	tmp_sd = accept(proc->sockfd, (struct sockaddr *)&addr, &addr_len);
	if(tmp_sd < 0)
	{
		if(errno == ECONNABORTED)
		{
			/* The process socket has just closed, discard its connect request */
			printf("\tDEBUG: connect_to_process - The process's socket has just closed, discard its connect request!\n");
			if(proc != NULL)
			{
				disconnect_from_process(proc);
			}
			return true;
		}

		printf("\tDEBUG: connect_to_process - Failed to accept socket connection on address %s!\n", proc->sockaddr.sun_path);
		return false;
	}

	res = close(proc->sockfd);
	if(res < 0)
	{
		printf("\tDEBUG: connect_to_process - Failed to close socket connection on address %s!\n", proc->sockaddr.sun_path);
		return false;
	}
	proc->sockfd = tmp_sd;

	res = send(tmp_sd, "ack", 4, 0);
	while(res < 0 && errno == EINTR)
	{
		res = send(tmp_sd, "ack", 4, 0);
	}
	if(res < 0)
	{
		if(errno == EPIPE)
		{
			/* The process socket has just closed, discard its connect request */
			printf("\tDEBUG: connect_to_process - The process's socket has just closed, not send ACK!\n");
			if(proc != NULL)
			{
				disconnect_from_process(proc);
			}
			return true;
		}

		printf("\tDEBUG: connect_to_process - Failed to send ACK to the process, pid = %d\n", proc->pid);
		return false;
	}

	printf("\tDEBUG: connect_to_process - Sending ACK signature to the process successfully, pid = %d\n", proc->pid);
	proc->state = PROC_CONNECTED;
	return true;
}

static void handle_incoming_request(void)
{
	union itc_msg *msg;

	msg = itc_receive(ITC_NO_WAIT);
	switch(msg->msgno)
	{
		case ITC_NOTIFY_COORD_ADD_MBOX:
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_ADD_MBOX received!\n");
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_ADD_MBOX mbox_id = 0x%08x\n", msg->itc_notify_coord_add_rmv_mbox.mbox_id);
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_ADD_MBOX mbox_name = %s!\n", msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			handle_add_mbox(msg->itc_notify_coord_add_rmv_mbox.mbox_id, msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			break;
		
		case ITC_NOTIFY_COORD_RMV_MBOX:
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_RMV_MBOX received!\n");
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_RMV_MBOX mbox_id = 0x%08x\n", msg->itc_notify_coord_add_rmv_mbox.mbox_id);
			printf("\tDEBUG: handle_incoming_request - ITC_NOTIFY_COORD_RMV_MBOX mbox_name = %s!\n", msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			handle_remove_mbox(msg->itc_notify_coord_add_rmv_mbox.mbox_id, msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			break;
		
		case ITC_LOCATE_MBOX_SYNC_REQUEST:
			printf("\tDEBUG: handle_incoming_request - ITC_LOCATE_MBOX_SYNC_REQUEST received!\n");
			printf("\tDEBUG: handle_incoming_request - ITC_LOCATE_MBOX_SYNC_REQUEST from_mbox = 0x%08x\n", msg->itc_locate_mbox_sync_request.from_mbox);
			printf("\tDEBUG: handle_incoming_request - ITC_LOCATE_MBOX_SYNC_REQUEST timeout = %d ms\n", msg->itc_locate_mbox_sync_request.timeout);
			printf("\tDEBUG: handle_incoming_request - ITC_LOCATE_MBOX_SYNC_REQUEST timeout = %d ms\n", msg->itc_locate_mbox_sync_request.find_only_internal);
			printf("\tDEBUG: handle_incoming_request - ITC_LOCATE_MBOX_SYNC_REQUEST mbox_name = %s!\n", msg->itc_locate_mbox_sync_request.mbox_name);
			handle_locate_mbox(msg->itc_locate_mbox_sync_request.from_mbox, msg->itc_locate_mbox_sync_request.timeout, msg->itc_locate_mbox_sync_request.find_only_internal, msg->itc_locate_mbox_sync_request.mbox_name);
			break;

		default:
			printf("\tDEBUG: handle_incoming_request - Unknown signal 0x%08x received, discard it!\n", msg->msgno);
			break;
	}

	itc_free(&msg);
}

static void handle_add_mbox(itc_mbox_id_t mbox_id, char *mbox_name)
{
	struct itc_process *proc;
	struct itc_mbox_info *mbox, **iter;

	proc = find_process(mbox_id);
	if(proc == NULL)
	{
		printf("\tDEBUG: handle_add_mbox - Mailbox 0x%08x from an unknown process!\n", mbox_id);
		return;
	} else if(proc->state != PROC_CONNECTED)
	{
		printf("\tDEBUG: handle_add_mbox - Mailbox 0x%08x from a just-terminated process!\n", mbox_id);
		return;
	}

	mbox = (struct itc_mbox_info *)malloc(offsetof(struct itc_mbox_info, mbox_name) + strlen(mbox_name) + 1);
	if(mbox == NULL)
	{
		printf("\tDEBUG: handle_add_mbox - Failed to add mailbox 0x%08x to itccoord mbox_tree due to out of memory!\n", mbox_id);
		return;
	}

	mbox->mbox_id = mbox_id;
	strcpy(mbox->mbox_name, mbox_name);

	iter = tfind(mbox, &itccoord_inst.mbox_tree, mbox_name_cmpfunc2);
	if(iter != NULL)
	{
		printf("\tDEBUG: handle_add_mbox - Mailbox 0x%08x already exists in mbox_tree, something was messed up!\n", mbox_id);
		return;
	} else
	{
		tsearch(mbox, &itccoord_inst.mbox_tree, mbox_name_cmpfunc2);
	}
}

static void handle_remove_mbox(itc_mbox_id_t mbox_id, char *mbox_name)
{
	struct itc_process *proc;
	struct itc_mbox_info **iter, *mbox;

	proc = find_process(mbox_id);
	if(proc == NULL)
	{
		printf("\tDEBUG: handle_remove_mbox - Mailbox 0x%08x from an unknown process!\n", mbox_id);
		return;
	} else if(proc->state != PROC_CONNECTED)
	{
		printf("\tDEBUG: handle_remove_mbox - Mailbox 0x%08x from a just-terminated process!\n", mbox_id);
		return;
	}

	iter = tfind(mbox_name, &itccoord_inst.mbox_tree, mbox_name_cmpfunc);
	if(iter == NULL)
	{
		printf("\tDEBUG: handle_remove_mbox - Mailbox 0x%08x not found in mbox_tree, something was messed up!\n", mbox_id);
		return;
	}

	mbox = *iter;
	tdelete(mbox_name, &itccoord_inst.mbox_tree, mbox_name_cmpfunc);
	free(mbox);
}

static void handle_locate_mbox(itc_mbox_id_t from_mbox, int32_t timeout, bool find_only_internal, char *mbox_name)
{
	struct itc_process *proc;
	struct itc_mbox_info **iter;
	pid_t pid;
	itc_mbox_id_t mbox_id;
	bool is_external = false;
	char namespace[ITC_MAX_NAME_LENGTH];
	strcpy(namespace, "");

	iter = tfind(mbox_name, &itccoord_inst.mbox_tree, mbox_name_cmpfunc);
	if(iter == NULL)
	{
		printf("\tDEBUG: handle_add_mbox - Mailbox 0x%08x not found in the mbox_tree, try to find it in other hosts!\n", (*iter)->mbox_id);
		// return; // Will not return here, anyhow still need to reply the process

		if(!find_only_internal)
		{
			union itc_msg *req;
			req = itc_alloc(offsetof(struct itc_locate_mbox_from_itcgws_request, mboxname) + strlen(mbox_name) + 1, ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST);
			strcpy(req->itc_locate_mbox_from_itcgws_request.mboxname, mbox_name);

			itc_mbox_id_t itcgw_mboxid = itc_locate_sync(timeout, ITC_GATEWAY_MBOX_TCP_CLI_NAME, 1, NULL, NULL);
			if(itcgw_mboxid == ITC_NO_MBOX_ID)
			{
				printf("\tDEBUG: itc_locate_sync_zz - Failed to locate mailbox \"%s\" even after %d ms!\n", ITC_GATEWAY_MBOX_TCP_CLI_NAME, timeout);
				itc_free(&req);
				return;
			}

			if(itc_send(&req, itcgw_mboxid, ITC_MY_MBOX_ID, NULL) == false)
			{
				printf("\tDEBUG: itc_locate_sync_zz - Failed to send ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST to itccoord!\n");
				itc_free(&req);
				return;
			}

			req = itc_receive(timeout);
			if(req == NULL)
			{
				printf("\tDEBUG: itc_get_namespace_zz - Failed to ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY even after %d ms!\n", timeout);
				return;
			} else if(req->msgno != ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY)
			{
				printf("\tDEBUG: itc_locate_sync_zz - Received unknown message 0x%08x, expecting ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY!\n", req->msgno);
				itc_free(&req);
				return;
			}

			mbox_id = req->itc_locate_mbox_from_itcgws_reply.mbox_id;
			is_external = true;
			strcpy(namespace, req->itc_locate_mbox_from_itcgws_reply.namespace);

			itc_free(&req);
		} else
		{
			mbox_id = ITC_NO_MBOX_ID;
			pid = -1;
		}
	} else
	{
		mbox_id = (*iter)->mbox_id;
		proc = find_process(mbox_id);
		if(proc == NULL)
		{
			printf("\tDEBUG: handle_locate_mbox - Received a locating mailbox request from from an unknown process's mailbox 0x%08x\n", from_mbox);
			return;
		} else if(proc->state != PROC_CONNECTED)
		{
			printf("\tDEBUG: handle_locate_mbox - Mailbox 0x%08x from a just-terminated process!\n", from_mbox);
			return;
		}

		pid = proc->pid;
	}

	union itc_msg *msg;
	msg = itc_alloc(offsetof(struct itc_locate_mbox_sync_reply, namespace) + strlen(namespace) + 1, ITC_LOCATE_MBOX_SYNC_REPLY);

	msg->itc_locate_mbox_sync_reply.mbox_id 	= mbox_id;
	msg->itc_locate_mbox_sync_reply.pid 		= pid;
	msg->itc_locate_mbox_sync_reply.is_external 	= is_external;
	strcpy(msg->itc_locate_mbox_sync_reply.namespace, namespace);

	/* Send back response to the process */
	if(itc_send(&msg, from_mbox, ITC_MY_MBOX_ID, NULL) == false)
	{
		printf("\tDEBUG: handle_locate_mbox - Failed to send ITC_LOCATE_MBOX_SYNC_REPLY to mailbox 0x%08x\n", from_mbox);
		itc_free(&msg);
		return;
	}

	printf("\tDEBUG: handle_locate_mbox - Sent ITC_LOCATE_MBOX_SYNC_REPLY to mailbox 0x%08x\n", from_mbox);
}

static int mbox_name_cmpfunc(const void *pa, const void *pb)
{
	const char *name = pa;
	const struct itc_mbox_info *mbox = pb;

	return strcmp(name, mbox->mbox_name);
}

static int mbox_name_cmpfunc2(const void *pa, const void *pb)
{
	const struct itc_mbox_info *mbox1 = pa;
	const struct itc_mbox_info *mbox2 = pb;

	return strcmp(mbox1->mbox_name, mbox2->mbox_name);
}

