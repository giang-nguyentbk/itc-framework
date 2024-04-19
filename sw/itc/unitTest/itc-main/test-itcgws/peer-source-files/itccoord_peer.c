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

#define ITC_GATEWAY_MBOX_TCP_CLI_NAME2	"itc_gw_tcp_client_mailbox2" // TEST ONLY
#define ITC_ITCCOORD_LOGFILE2 		"itccoord.log" // TEST ONLY

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
static bool setup_log_file(void);
static bool setup_rc(void);
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

	itccoord_init();

	int opt = 0;
	bool is_daemon = false;

	while((opt = getopt(argc, argv, "d")) != -1)
	{
		switch (opt)
		{
		case 'd':
			is_daemon = true;
			break;
		
		default:
			printf("ERROR: Usage:\t%s\t[-d]\n", argv[0]);
			printf("Example:\t%s\t-d\n", argv[0]);
			printf("=> This will start itccoord as a daemon!\n");
			exit(EXIT_FAILURE);
			break;
		}
	}

	if(is_daemon)
	{
		printf(">>> Starting itccoord daemon...\n");

		if(!setup_log_file())
		{
			ITC_ERROR("Failed to setup log file for this itccoord daemon!");
			exit(EXIT_FAILURE);
		}

		if(daemon(1, 1))
		{
			ITC_ERROR("Failed to start itccoord as a daemon!");
			exit(EXIT_FAILURE);
		}

		ITC_INFO("Starting itccoord daemon...");
	} else
	{
		ITC_INFO("Starting itccoord, but not as a daemon...");
	}

	// At normal termination we just clean up our resources by registration a exit_handler
	atexit(itccoord_exit_handler);

	if(!setup_rc())
	{
		ITC_ERROR("Failed to setup rc!");
		exit(EXIT_FAILURE);
	}

	if(is_itccoord_running() == true)
	{
		/* itccoord is already running 
		** If it has died unexpectedly, you may need to remove /tmp/itc/itccoord/itc_coordinator and restart it again */
		ITC_ERROR("ITCCOORD already running, no need to start again!");
		exit(EXIT_FAILURE);
	}

	if(create_itccoord_dir() == false)
	{
		ITC_ERROR("Failed to create_itccoord_dir()!");
		exit(EXIT_FAILURE);
	}

	// Allocate two mailboxes, one is for itccoord-self, one is reserved
	if(itc_init(2, ITC_MALLOC, ITC_FLAGS_I_AM_ITC_COORD) == false)
	{
		ITC_ERROR("Failed to itc_init(), ITC_FLAGS_I_AM_ITC_COORD!");
		exit(EXIT_FAILURE);
	}

	itccoord_inst.mbox_id = itc_create_mailbox(ITC_COORD_MBOX_NAME, 0);
	if(itccoord_inst.mbox_id == ITC_NO_MBOX_ID)
	{
		ITC_ERROR("Failed to itc_create_mailbox(), mbox_name = %s!", ITC_COORD_MBOX_NAME);
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
	ITC_INFO("Enqueue process index = %d to free_list!", i);
	ITC_INFO("Enqueue process n-th to free_list!");
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
	ITC_INFO("Enqueue process index = %d to free_list!", i - 1);

	itccoord_inst.sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(itccoord_inst.sockfd == -1)
	{
		ITC_ERROR("Failed to socket(), errno = %d!", errno);
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
		ITC_ERROR("Failed to bind(), errno = %d!", errno);
		exit(EXIT_FAILURE);
	}

	res = chmod(ITC_ITCCOORD_FILENAME, 0777); // Using socket we do not actually need to fopen "/tmp/itc/itccoord/itc_coordinator" by ourselves
	if(res < 0)
	{
		ITC_ERROR("Failed to chmod(), errno = %d!", errno);
		exit(EXIT_FAILURE);
	}

	res = listen(itccoord_inst.sockfd, 10); // Listening and waiting for locating itccoord from other processes, maximum 10 processes will be queued at a time
	if(res < 0)
	{
		ITC_ERROR("Failed to listen(), errno = %d!", errno);
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
			ITC_ERROR("Failed to select(), errno = %d!", errno);
			exit(EXIT_FAILURE);
		} else
		{
			if(FD_ISSET(itccoord_inst.sockfd, &proc_fd_list))
			{
				if(handle_locate_coord_request(itccoord_inst.sockfd) == false)
				{
					ITC_ERROR("Failed to handle_locate_coord_request()!");
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
						ITC_INFO("Process with fd %d is in state PROC_LISTENING!", proc->sockfd);
						if(connect_to_process(proc) == false)
						{
							exit(EXIT_FAILURE);
						}
					} else if(proc->state == PROC_CONNECTED)
					{
						ITC_INFO("Process with fd %d is in state PROC_CONNECTED!", proc->sockfd);
						if(disconnect_from_process(proc) == false)
						{
							exit(EXIT_FAILURE);
						}
						break;
					} else
					{
						ITC_ERROR("Wrong process state = %d, pid = %d!", proc->state, proc->pid);
						exit(EXIT_FAILURE);
					}
				} else if((zc_counter == (NR_PROC_ALIVENESS_CHECK_RETRIES - 1) || itccoord_inst.freelist_count < FREELIST_LOW_THRESHOLD) && \
					proc->state == PROC_LISTENING && proc->pid > 0)
				{
					/* Try killing the process but received errno no such process */
					if(kill(proc->pid, 0) == -1 && errno == ESRCH)
					{
						ITC_ABN("Zomebie process detected, pid = %d!", proc->pid);
						disconnect_from_process(proc);
						zc_counter--;
						break;
					}
				}
			}

			if(FD_ISSET(itccoord_inst.mbox_fd, &proc_fd_list))
			{
				ITC_INFO("Calling handle_incoming_request()!");
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
	ITC_INFO("ITCCOORD is terminated with SIG = %d, calling exit handler...", signo);
	itccoord_exit_handler();

	// After clean up, resume raising the suppressed signal
	signal(signo, SIG_DFL); // Inform kernel does fault exit_handler for this kind of signal
	raise(signo);
}

static void itccoord_exit_handler(void)
{
	ITC_INFO("ITCCOORD is terminated, calling exit handler...");
	struct itc_process *proc;

	ITC_INFO("Removing all sockets on all active processes...");
	for(int i = 0; i < MAX_SUPPORTED_PROCESSES; i++)
	{
		/* Close socket connection for all processes */
		proc = find_process(i << ITC_COORD_SHIFT);
		if(close_socket_connection(proc) == false)
		{
			ITC_ERROR("Failed to close socket for pid = %d", proc->pid);
			return;
		}
	}


	unlink(ITC_ITCCOORD_FILENAME);
	rmdir(ITC_SOCKET_FOLDER);
	rmdir(ITC_ITCCOORD_FOLDER);
	remove(ITC_SYSVMSQ_FILENAME);
	rmdir(ITC_SYSVMSQ_FOLDER);
	rmdir(ITC_BASE_PATH);
	ITC_INFO("Remove all directories successfully!");

	tdestroy(itccoord_inst.mbox_tree, free); // Call free() on each node's user data
	ITC_INFO("Remove all nodes from mbox_tree successfully!");

	ITC_INFO("Removing processes in free_list, count = %u!", itccoord_inst.freelist_count);
	q_exit(rc, itccoord_inst.free_list);
	ITC_INFO("Removing processes in used_list, count = %u!", 253 - itccoord_inst.freelist_count);
	q_exit(rc, itccoord_inst.used_list);

	itc_delete_mailbox(itccoord_inst.mbox_id);
	itc_exit();

	free(rc);
}

static bool setup_log_file(void)
{
	/* Setup a log file for our itcgws daemon */
	freopen(ITC_ITCCOORD_LOGFILE2, "a+", stdout); // TEST ONLY
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stderr);

	fprintf(stdout, "========================================================================================================================\n");
	fflush(stdout);
	fprintf(stdout, ">>>>>>>                                             START NEW SESSION                                            <<<<<<<\n");
	fflush(stdout);
	fprintf(stdout, "========================================================================================================================\n");
	fflush(stdout);

	return true;
}

static bool setup_rc(void)
{
	if(rc == NULL)
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
			ITC_ERROR("Failed to malloc rc due to out of memory!");
                	return false;
		}	
	}

	rc->flags = ITC_OK;
	return true;
}

static bool is_itccoord_running(void)
{
	struct stat s;
	char itccoodinator_path[] = ITC_ITCCOORD_FOLDER;

	if((stat(itccoodinator_path, &s) == 0) || errno != ENOENT)
	{
		// itccoord is running
		ITC_INFO("ITCCOORD already running!");
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
		ITC_ERROR("Failed to mkdir %s, errno = %d!", ITC_BASE_PATH, errno);
		return false;
	}

	res = chmod(ITC_BASE_PATH, 0777);
	if(res < 0)
	{
		ITC_ERROR("Failed to chmod %s, errno = %d!", ITC_BASE_PATH, errno);
		return false;
	}

	res = mkdir(ITC_SOCKET_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		ITC_ERROR("Failed to mkdir %s, errno = %d!", ITC_SOCKET_FOLDER, errno);
		return false;
	}

	res = chmod(ITC_SOCKET_FOLDER, 0777);
	if(res < 0)
	{
		ITC_ERROR("Failed to chmod %s, errno = %d!", ITC_SOCKET_FOLDER, errno);
		return false;
	}

	res = mkdir(ITC_ITCCOORD_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		ITC_ERROR("Failed to mkdir %s, errno = %d!", ITC_ITCCOORD_FOLDER, errno);
		return false;
	}

	res = chmod(ITC_ITCCOORD_FOLDER, 0777);
	if(res < 0)
	{
		ITC_ERROR("Failed to chmod %s, errno = %d!", ITC_ITCCOORD_FOLDER, errno);
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
			ITC_ABN("ECONNABORTED, OK!");
			return true;
		}

		ITC_ERROR("Failed to accept socket connection on sd = %d!", sd);
		return false;
	}

	size_t RXLEN = 1024;
	lrequest = (struct itc_locate_coord_request*)malloc(RXLEN); // Allocate a rx buffer to receive the request from the process
	if(lrequest == NULL)
	{
		ITC_ERROR("Failed to malloc itc_locate_coord_request due to out of memory!");
		return false;
	}

	len = recv(tmp_sd, lrequest, RXLEN, 0);
	if(len < (size_t)sizeof(struct itc_locate_coord_request))
	{
		if(errno == EIDRM || errno == ENOENT || errno == EINVAL)
		{
			/* The reason could be that the socket connection of the process was just closed 
			** So ignore the request */
			ITC_ABN("The process's socket has just closed, OK!");
			free(lrequest);
			return true;
		}

		/* Not a big problem here so not return */
		lrequest->my_pid = -1;
	} else if(lrequest->msgno != ITC_LOCATE_COORD_REQUEST)
	{
		/* Received unknown request from the process */
		ITC_ERROR("Unknown message received, expected ITC_LOCATE_COORD_REQUEST!");
		return false;
	}

	ITC_INFO("Dequeue a process from free_list!");
	tmp = q_dequeue(rc, itccoord_inst.free_list);
	if(rc->flags != ITC_OK)
	{
		ITC_ERROR("Failed to dequeue a process from free_list, rc = %u!", rc->flags);
		return false;
	}

	if(tmp != NULL)
	{
		itccoord_inst.freelist_count--;
		ITC_INFO("Enqueue process mbox_id = 0x%08x to used_list!", tmp->mbox_id_in_itccoord);
		q_enqueue(rc, itccoord_inst.used_list, tmp);
		if(rc->flags != ITC_OK)
		{
			ITC_ERROR("Failed to enqueue a process to used_list, rc = %u!", rc->flags);
			return false;
		}


		tmp->pid = lrequest->my_pid;

		/* Create a new socket to keep communication with the process later on */
		tmp->sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
		if(tmp->sockfd < 0)
		{
			ITC_ERROR("Failed to create socket, errno = %d!", errno);
			return false;
		}
		memset(&tmp->sockaddr, 0, sizeof(struct sockaddr_un));
		tmp->sockaddr.sun_family = AF_LOCAL;
		sprintf(tmp->sockaddr.sun_path, "%s_0x%08x", ITC_LSOCKET_FILENAME, tmp->mbox_id_in_itccoord);

		res = bind(tmp->sockfd, (struct sockaddr*)&tmp->sockaddr, sizeof(tmp->sockaddr));
		if(res < 0)
		{
			ITC_ERROR("Failed to bind socket, errno = %d!", errno);
			return false;
		}

		res = chmod(tmp->sockaddr.sun_path, 0777);
		if(res < 0)
		{
			ITC_ERROR("Failed to chmod %s, errno = %d!", tmp->sockaddr.sun_path, errno);
			return false;
		}

		res = listen(tmp->sockfd, 1);
		if(res < 0)
		{
			ITC_ERROR("Failed to listen, errno = %d!", errno);
			return false;
		}

		tmp->state = PROC_LISTENING;
	}

	/* Send back response to the process */
	lreply = (struct itc_locate_coord_reply*)malloc(sizeof(struct itc_locate_coord_reply));
	if(lreply == NULL)
	{
		ITC_ERROR("Failed to malloc lreply due to out of memory!");
		return false;
	}

	lreply->msgno = ITC_LOCATE_COORD_REPLY;
	if(tmp == NULL)
	{
		ITC_ABN("No more process to allocate for this ITC_LOCATE_COORD_REQUEST!");
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
			ITC_ERROR("Failed to send itc_locate_coord_reply to process pid = %d due to EPIPE!", tmp->pid);
			free(lreply);
			free(lrequest);
			if(tmp != NULL)
			{
				disconnect_from_process(tmp);
			}

			ITC_ERROR("Failed to send itc_locate_coord_reply, EPIPE!");
			return false;
		}

		ITC_ERROR("Failed to send itc_locate_coord_reply, res < 0!");
		return false;
	}

	ITC_INFO("Sent itc_locate_coord_reply successfully!");

	free(lreply);
	free(lrequest);

	res = close(tmp_sd);
	if(res < 0)
	{
		ITC_ERROR("Failed to close, res = %d, errno = %d!", res, errno);
		return false;
	}

	return true;
}

static bool disconnect_from_process(struct itc_process *proc)
{
	/* Iterating through the used queue and search for any node that points to proc
	** Then remove it and concatenate the prev and the next of node iter */
	ITC_INFO("Remove process mbox_id = 0x%08x from used_list!", proc->mbox_id_in_itccoord);
	q_remove(rc, itccoord_inst.used_list, proc);
	proc->state = PROC_UNUSED;

	/* Close socket connection is for the corresponding process */
	if(close_socket_connection(proc) == false)
	{
		ITC_ERROR("Failed to close socket connection for process mbox_id = 0x%08x", proc->mbox_id_in_itccoord);
		return false;
	}

	// remove_all_mbox_in_process(proc->mbox_id_in_itccoord);

	memset(&proc->sockaddr, 0, sizeof(struct sockaddr_un));
	ITC_INFO("Enqueue process mbox_id = 0x%08x to free_list!", proc->mbox_id_in_itccoord);
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

		ITC_INFO("Remove socket on process pid = %d", proc->pid);
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
					ITC_ABN("Could not close_socket_connection - errno = EBADF, but it's OK!");
					return true;
				} else
				{
					ITC_ERROR("Failed to close_socket_connection - errno = %d, NOK!", errno);
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
				ITC_ERROR("Failed to unlink socket address, errno = %d!", errno);
				return false;
			} else
			{
				ITC_ABN("No such file or directory, not unlink socket address %s!", ITC_LSOCKET_FILENAME);
			}
		} else
		{
			ITC_INFO("Unlink %s successfully!", ITC_LSOCKET_FILENAME);
		}
	}

	return true;
}

static struct itc_process *find_process(itc_mbox_id_t mbox_id)
{
	int index;

	index = ((mbox_id & ITC_COORD_MASK) >> ITC_COORD_SHIFT);

	if(index > MAX_SUPPORTED_PROCESSES)
	{
		ITC_ERROR("Invalid mbox_id = 0x%08x > MAX_SUPPORTED_PROCESSES = 255!", mbox_id);
		return NULL;
	}

	return &itccoord_inst.processes[index];
}

static bool connect_to_process(struct itc_process *proc)
{
	struct sockaddr_un addr;
	socklen_t addr_len = sizeof(struct sockaddr_un);
	int tmp_sd, res;

	ITC_INFO("Connecting to the process with pid = %d", proc->pid);

	tmp_sd = accept(proc->sockfd, (struct sockaddr *)&addr, &addr_len);
	if(tmp_sd < 0)
	{
		if(errno == ECONNABORTED)
		{
			/* The process socket has just closed, discard its connect request */
			ITC_ABN("The process's socket has just closed, discard its connect request!");
			if(proc != NULL)
			{
				disconnect_from_process(proc);
			}
			return true;
		}

		ITC_ERROR("Failed to accept socket connection on address %s!", proc->sockaddr.sun_path);
		return false;
	}

	res = close(proc->sockfd);
	if(res < 0)
	{
		ITC_ERROR("Failed to close socket connection on address %s!", proc->sockaddr.sun_path);
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
			ITC_ABN("The process's socket has just closed, will not send ACK!");
			if(proc != NULL)
			{
				disconnect_from_process(proc);
			}
			return true;
		}

		ITC_ERROR("Failed to send ACK to the process, pid = %d", proc->pid);
		return false;
	}

	ITC_INFO("Sending ACK signature to the process successfully, pid = %d", proc->pid);
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
			ITC_INFO("ITC_NOTIFY_COORD_ADD_MBOX received!");
			ITC_INFO("ITC_NOTIFY_COORD_ADD_MBOX mbox_id = 0x%08x", msg->itc_notify_coord_add_rmv_mbox.mbox_id);
			ITC_INFO("ITC_NOTIFY_COORD_ADD_MBOX mbox_name = %s!", msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			handle_add_mbox(msg->itc_notify_coord_add_rmv_mbox.mbox_id, msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			break;
		
		case ITC_NOTIFY_COORD_RMV_MBOX:
			ITC_INFO("ITC_NOTIFY_COORD_RMV_MBOX received!");
			ITC_INFO("ITC_NOTIFY_COORD_RMV_MBOX mbox_id = 0x%08x", msg->itc_notify_coord_add_rmv_mbox.mbox_id);
			ITC_INFO("ITC_NOTIFY_COORD_RMV_MBOX mbox_name = %s!", msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			handle_remove_mbox(msg->itc_notify_coord_add_rmv_mbox.mbox_id, msg->itc_notify_coord_add_rmv_mbox.mbox_name);
			break;
		
		case ITC_LOCATE_MBOX_SYNC_REQUEST:
			ITC_INFO("ITC_LOCATE_MBOX_SYNC_REQUEST received!");
			ITC_INFO("ITC_LOCATE_MBOX_SYNC_REQUEST from_mbox = 0x%08x", msg->itc_locate_mbox_sync_request.from_mbox);
			ITC_INFO("ITC_LOCATE_MBOX_SYNC_REQUEST timeout = %d ms", msg->itc_locate_mbox_sync_request.timeout);
			ITC_INFO("ITC_LOCATE_MBOX_SYNC_REQUEST timeout = %d ms", msg->itc_locate_mbox_sync_request.find_only_internal);
			ITC_INFO("ITC_LOCATE_MBOX_SYNC_REQUEST mbox_name = %s!", msg->itc_locate_mbox_sync_request.mbox_name);
			handle_locate_mbox(msg->itc_locate_mbox_sync_request.from_mbox, msg->itc_locate_mbox_sync_request.timeout, msg->itc_locate_mbox_sync_request.find_only_internal, msg->itc_locate_mbox_sync_request.mbox_name);
			break;

		default:
			ITC_ABN("Unknown signal 0x%08x received, discard it!", msg->msgno);
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
		ITC_ERROR("Mailbox 0x%08x from an unknown process!", mbox_id);
		return;
	} else if(proc->state != PROC_CONNECTED)
	{
		ITC_ERROR("Mailbox 0x%08x from a just-terminated process!", mbox_id);
		return;
	}

	mbox = (struct itc_mbox_info *)malloc(offsetof(struct itc_mbox_info, mbox_name) + strlen(mbox_name) + 1);
	if(mbox == NULL)
	{
		ITC_ERROR("Failed to add mailbox 0x%08x to itccoord mbox_tree due to out of memory!", mbox_id);
		return;
	}

	mbox->mbox_id = mbox_id;
	strcpy(mbox->mbox_name, mbox_name);

	iter = tfind(mbox, &itccoord_inst.mbox_tree, mbox_name_cmpfunc2);
	if(iter != NULL)
	{
		ITC_ERROR("Mailbox 0x%08x already exists in mbox_tree, something was messed up!", mbox_id);
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
		ITC_ERROR("Mailbox 0x%08x from an unknown process!", mbox_id);
		return;
	} else if(proc->state != PROC_CONNECTED)
	{
		ITC_ABN("Mailbox 0x%08x from a just-terminated process!", mbox_id);
		return;
	}

	iter = tfind(mbox_name, &itccoord_inst.mbox_tree, mbox_name_cmpfunc);
	if(iter == NULL)
	{
		ITC_ERROR("Mailbox 0x%08x not found in mbox_tree, something was messed up!", mbox_id);
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
	itc_mbox_id_t itcgw_mboxid = ITC_NO_MBOX_ID;
	bool is_external = false;
	char namespace[ITC_MAX_NAME_LENGTH];
	strcpy(namespace, "");

	iter = tfind(mbox_name, &itccoord_inst.mbox_tree, mbox_name_cmpfunc);
	if(iter == NULL)
	{
		ITC_INFO("Mailbox \"%s\" not found in the mbox_tree, try to find it in other hosts!", mbox_name);
		// return; // Will not return here, anyhow still need to reply the process

		if(!find_only_internal)
		{
			union itc_msg *req;
			req = itc_alloc(offsetof(struct itc_locate_mbox_from_itcgws_request, mboxname) + strlen(mbox_name) + 1, ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST);
			req->itc_locate_mbox_from_itcgws_request.itccoord_mboxid = itccoord_inst.mbox_id;
			strcpy(req->itc_locate_mbox_from_itcgws_request.mboxname, mbox_name);

			iter = tfind(ITC_GATEWAY_MBOX_TCP_CLI_NAME2, &itccoord_inst.mbox_tree, mbox_name_cmpfunc); // TEST ONLY
			if(iter == NULL)
			{
				ITC_ERROR("Failed to locate mailbox \"%s\"", ITC_GATEWAY_MBOX_TCP_CLI_NAME2); // TEST ONLY
				itc_free(&req);
				return;
			} else
			{
				itcgw_mboxid = (*iter)->mbox_id;
			}

			if(itc_send(&req, itcgw_mboxid, ITC_MY_MBOX_ID, NULL) == false)
			{
				ITC_ERROR("Failed to send ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST to itccoord!");
				itc_free(&req);
				return;
			}

			ITC_INFO("Sent ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST to itcgws successfully!");

			req = itc_receive(timeout);
			if(req == NULL)
			{
				ITC_ERROR("Failed to ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY even after %d ms!", timeout);
				return;
			} else if(req->msgno != ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY)
			{
				ITC_ABN("Received unknown message 0x%08x, expecting ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY!", req->msgno);
				itc_free(&req);
				return;
			}

			ITC_INFO("Received ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY to itcgws successfully!");
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
			ITC_ERROR("Received a locating mailbox request from from an unknown process's mailbox 0x%08x", from_mbox);
			return;
		} else if(proc->state != PROC_CONNECTED)
		{
			ITC_ABN("Mailbox 0x%08x from a just-terminated process!", from_mbox);
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
		ITC_ERROR("Failed to send ITC_LOCATE_MBOX_SYNC_REPLY to mailbox 0x%08x", from_mbox);
		itc_free(&msg);
		return;
	}

	ITC_INFO("Sent ITC_LOCATE_MBOX_SYNC_REPLY to mailbox 0x%08x", from_mbox);
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

