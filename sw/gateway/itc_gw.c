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
#include <regex.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#include "itc.h"
#include "itc_impl.h"
#include "itc_queue.h"
#include "itc_proto.h"
#include "itc_gw_proto.h"



/*
	1. itc_gw will be run as a daemon
	2. Include:
		+ A first process which is runing main function, fork to create a daemon, and kill itself to make daemon run on background.
		
		+ Daemon will first config:
			1. Create a UDP SOCK_DGRAM socket on port 11111 used for broadcasting greeting messages.
			2. Create a TCP SOCK_STREAM socket on port 22222 used for exchanging Ethernet packages between hosts (external-side).
			3. Create a mailbox named "itc_gw" used for exchanging itc_msg between processes in our host (internal-side).

		+ Daemon will run a while true loop to do:
			1. Broadcasting greeting messages:
				+ At first run of while loop and at every 50000-th while loop.
				+ The greeting message is something like "Are you an itc_gw like me? If yes and want to talk, here is my contact, just connect to me via this TCP socket".
				If any host would like to connect to us, it will connect to this TCP socket and give us their namespace, ip address, port,...
			2. Monitor all fd including (listening TCP socket, broadcast UDP socket, mailbox fd and all fds for accepted connections):
				+ In first version, using select().
			3. If select() finds that we are receiving greeting messages on broadcasting port 11111:
				+ Send TCP connection request to the TCP port in greeting messages.
			4. If select() finds that we are receiving something on TCP listening port 22222:
				+ If received messages are connection requests:
					-> Send back a reply that confirms connection is accepted.
				+ If received messages are data messages:
					-> Check if the namespace in the message is matched with our namespace.
					-> If yes, accept the message, and forward it to our respective mailbox. If not, discard it!
				+ If receive nothing, that means the partner's socket connection has closed.
					-> Close our socket fd as well.
			5. If select() finds that we are receiving itc_msg on "itc_gw" mailbox fd.
				+ Check the receiver's namespace and obtain the corresponding ip address based on targeted namespace.
				+ Send the message to the corresponding TCP accepted socket fd based on ip address.
*/







/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
#define ITCGW_BROADCAST_PORT		11111
#define ITCGW_LISTENING_PORT		22222
#define ITCGW_NETWORK_INTERFACE_ETH0	"eth0"
#define ITCGW_ETHERNET_PACKET_SIZE	1500
#define ITCGW_BROADCAST_INTERVAL	50000
#define ITCGW_PROT_REV_1		1

union itc_msg {
	uint32_t				msgno;

	struct itc_fwd_data_to_itcgws		itc_fwd_data_to_itcgws;
};



typedef enum {
	HOST_DISCONNECTED = 0, // After a host close() connection from us and we close() from our side as well
	HOST_CONNECTING, // After receiving greeting message, send connect() request and itc_host_connect_request, waiting for reply message
	HOST_CONNECTED, // After a host connect() to us and we accept their connection and send back itc_host_connect_reply
	HOST_INVALID
} host_state_e;


struct itc_host_info {
	char			*namespace;
	struct sockaddr_in	tcp_addr;
	int			sockfd;
	host_state_e		state;
};


struct itcgw_instance {
	struct itc_queue	*free_list;
	struct itc_queue	*used_list;

	uint32_t		freelist_count;
	struct itc_host_info	hosts[MAX_SUPPORTED_PROCESSES];

	void			*host_tree; // Used for quickly searching the requested locating mailboxes

	itc_mbox_id_t		mbox_id;
	int			mbox_fd; // Mailbox fd
	int			tcp_fd; // Public listening TCP fd
	int			udp_fd; // Broadcasting UDP fd

	struct sockaddr_in	tcp_addr; // Our info about ip address and port for public listening TCP port
	struct sockaddr_in	udp_addr; // Our info about ip address and port for broadcasting UDP port

	char			*namespace; // Our host's namespace
};




/*****************************************************************************\/
*****                     INTERNAL VARIABLES IN ITC.C                      *****
*******************************************************************************/
static struct itcgw_instance itcgw_inst;
static __thread struct result_code* rc = NULL; // A thread only owns one return code






/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void itcgw_init(void);
static void itcgw_sig_handler(int signo);
static void itcgw_exit_handler(void);
static bool setup_broadcast_fd(void);
static bool setup_listening_fd(void);
static bool handle_receive_greeting_message(int udpfd);
static bool handle_accept_incoming_connection(int tcpfd);
static int recv_data(int sockfd, void *rx_buff, int nr_bytes_to_read);
static bool handle_receive_itc_msg_forward(void);
static struct itc_host_info *locate_host_in_tree(char *namespace);
static int namespace_cmpfunc(const void *pa, const void *pb);
static bool handle_receive_get_namespace(struct itc_host_info *from_host);




/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	itcgw_init();

	if(rc == NULL)
	{
		rc = (struct result_code*)malloc(sizeof(struct result_code));
		if(rc == NULL)
		{
			printf("\tDEBUG: itcgw - Failed to malloc rc, OOM!\n");
                	exit(EXIT_FAILURE);
		}	
	}

	rc->flags = ITC_OK;

	// At normal termination we just clean up our resources by registration a exit_handler
	atexit(itcgw_exit_handler);

	/* Create a mailbox named "itc_gw" used for exchanging itc_msg between processes in our host (internal-side). */
	// Allocate two mailboxes, one is for itcgw-self, one is reserved
	if(itc_init(2, ITC_MALLOC, 0) == false)
	{
		printf("\tDEBUG: itcgw - Failed to itc_init() by ITCGW!\n");
		exit(EXIT_FAILURE);
	}

	itcgw_inst.mbox_id = itc_create_mailbox(ITC_GATEWAY_MBOX_NAME, 0);
	if(itcgw_inst.mbox_id == ITC_NO_MBOX_ID)
	{
		printf("\tDEBUG: itcgw - Failed to itc_create_mailbox(), mbox_name = %s!\n", ITC_GATEWAY_MBOX_NAME);
		exit(EXIT_FAILURE);
	}

	itcgw_inst.mbox_fd = itc_get_fd(itcgw_inst.mbox_id);

	itcgw_inst.free_list = q_init(rc);
	CHECK_RC_EXIT(rc);
	itcgw_inst.used_list = q_init(rc);
	CHECK_RC_EXIT(rc);

	/* Go through all other host's reserved slots and assign init value and enqueue to free list */
	int i = 0;
	struct itc_host_info *host;
	printf("\tDEBUG: itcgw - Enqueue host index = %d to free_list!\n", i);
	printf("\tDEBUG: itcgw - Enqueue host n-th to free_list!\n");
	for(; i < MAX_SUPPORTED_PROCESSES; i++)
	{
		host 				= &itcgw_inst.hosts[i];
		memset(host, 0, sizeof(struct itc_host_info));

		host->sockfd			= -1;
		host->state			= HOST_INVALID;
		strcpy(host->namespace, "");

		q_enqueue(rc, itcgw_inst.free_list, host);
		CHECK_RC_EXIT(rc);
		itcgw_inst.freelist_count++;
	}
	printf("\tDEBUG: itcgw - Enqueue host index = %d to free_list!\n", i - 1);

	/* Create a UDP SOCK_DGRAM socket on port 11111 used for broadcasting greeting messages. */
	if(setup_broadcast_fd() == false)
	{
		printf("\tDEBUG: itcgw - Failed to setup broadcast socket!\n");
		exit(EXIT_FAILURE);
	}

	/* Create a TCP SOCK_STREAM socket on port 22222 used for exchanging Ethernet packages between hosts (external-side). */
	if(setup_listening_fd() == false)
	{
		printf("\tDEBUG: itcgw - Failed to setup listening socket!\n");
		exit(EXIT_FAILURE);
	}

	if(itcgw_inst.udp_fd < 0)
	{
		printf("\tDEBUG: itcgw - itcgw_inst.udp_fd < 0!\n");
		exit(EXIT_FAILURE);
	}

	struct ifreq ifrq;
	memset(&ifrq, 0, sizeof(struct ifreq));
	int size = strlen(ITCGW_NETWORK_INTERFACE_ETH0) + 1;
	memcpy(&(ifrq.ifr_ifrn.ifrn_name), ITCGW_NETWORK_INTERFACE_ETH0, size);

	/* Get IP address from ITCGW_NETWORK_INTERFACE_ETH0 */
	size = sizeof(struct ifreq);
	int res = ioctl(itcgw_inst.udp_fd, SIOCGIFADDR, (caddr_t)&ifrq, size);
	if(res < 0)
	{
		printf("\tDEBUG: itcgw - Failed to ioctl to obtain IP address from %s, errno = %d!\n", ITCGW_NETWORK_INTERFACE_ETH0, errno);
		exit(EXIT_FAILURE);
	}

	size = sizeof(struct sockaddr_in);
	struct sockaddr_in server_addr;
	memcpy(&server_addr, &(ifrq.ifr_ifru.ifru_addr), size);

	printf("\tDEBUG: itcgw - Server address IP address from tcp://%s:%d\n", inet_ntoa(server_addr), ITCGW_LISTENING_PORT);
	printf("\tDEBUG: itcgw - TCP address IP address from tcp://%s:%d\n", inet_ntoa(itcgw_inst.tcp_addr), ITCGW_LISTENING_PORT);
	printf("\tDEBUG: itcgw - UDP address IP address from tcp://%s:%d\n", inet_ntoa(itcgw_inst.udp_addr), ITCGW_LISTENING_PORT);

	char *greeting_msg = malloc(ITCGW_ETHERNET_PACKET_SIZE);
	if(greeting_msg == NULL)
	{
		printf("\tDEBUG: itcgw - Failed to malloc greeting message!\n");
		exit(EXIT_FAILURE);
	}

	int greeting_msg_length = snprintf((char *)greeting_msg, ITCGW_ETHERNET_PACKET_SIZE, "ITC Gateway Broadcast Greeting Message from tcp://%s:%d/", \
		inet_ntoa(server_addr), ITCGW_LISTENING_PORT);
	
	/* This is address configuration of partner broadcast host */
	struct sockaddr_in broadcast_addr;
	memset(&broadcast_addr, 0, sizeof(struct sockaddr_in));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_port = htons((short)(ITCGW_BROADCAST_PORT & 0xFFFF));
	broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

	fd_set fd_set;
	int max_fd;
	uint32_t counter = ITCGW_BROADCAST_INTERVAL;
	struct itcq_node *iter;
	while(true)
	{
		/* 1. Broadcasting greeting messages: */
		if(counter >= ITCGW_BROADCAST_INTERVAL)
		{
			res = sendto(itcgw_inst.udp_fd, greeting_msg, greeting_msg_length, 0, (struct sockaddr *)((void *)&broadcast_addr), sizeof(struct sockaddr_in));
			if(res < 0)
			{
				printf("\tDEBUG: itcgw - Failed to broadcast greeting message on address tcp://%s:%d, errno = %d!\n", inet_ntoa(server_addr), ITCGW_LISTENING_PORT, errno);
				exit(EXIT_FAILURE);
			}

			/* Reset counter */
			counter = 0;
		}

		/* 2. Monitor all fd including (listening TCP socket, broadcast UDP socket, mailbox fd and all fds for accepted connections): */
		FD_ZERO(&fd_set);
		/* Add TCP fd */
		FD_SET(itcgw_inst.tcp_fd, &fd_set);
		max_fd = itcgw_inst.tcp_fd + 1;
		/* Add UDP fd */
		FD_SET(itcgw_inst.udp_fd, &fd_set);
		max_fd = itcgw_inst.udp_fd >= max_fd ? (itcgw_inst.udp_fd + 1) : max_fd;
		/* Add Mailbox fd */
		FD_SET(itcgw_inst.mbox_fd, &fd_set);
		max_fd = itcgw_inst.mbox_fd >= max_fd ? (itcgw_inst.mbox_fd + 1) : max_fd;

		/* Add accepted connection fd */
		for(iter = itcgw_inst.used_list->head; iter != NULL; iter = iter->next)
		{
			host = (struct itc_host_info *)iter->p_data;
			if(host->sockfd != -1)
			{
				FD_SET(host->sockfd, &fd_set);
				max_fd = host->sockfd ? (host->sockfd + 1) : max_fd;
			}
		}

		// Monitor those fd to see if any incoming data on them
		res = select(max_fd, &fd_set, NULL, NULL, NULL);
		if(res < 0)
		{
			printf("\tDEBUG: itcgw - Failed to select(), errno = %d!\n", errno);
			exit(EXIT_FAILURE);
		} else
		{
			/* 3. If select() finds that we are receiving greeting messages on broadcasting port 11111: */
			/* 	+ Send TCP get namespace request to the TCP port in greeting messages. */
			if(FD_ISSET(itcgw_inst.udp_fd, &fd_set))
			{
				if(handle_receive_greeting_message(itcgw_inst.udp_fd) == false)
				{
					printf("\tDEBUG: itcgw - Failed to handle_receive_greeting_message()!\n");
					exit(EXIT_FAILURE);
				}
			} else if(FD_ISSET(itcgw_inst.tcp_fd, &fd_set))
			{
				/* + If received messages are connection requests: */
				if(handle_accept_incoming_connection(itcgw_inst.tcp_fd) == false)
				{
					printf("\tDEBUG: itcgw - Failed to handle_accept_incoming_connection()!\n");
					exit(EXIT_FAILURE);
				}
				
			} else if(FD_ISSET(itcgw_inst.mbox_fd, &fd_set))
			{
				/* 5. If select() finds that we are receiving itc_msg on "itc_gw" mailbox fd. */
				printf("\tDEBUG: itcgw - Calling handle_incoming_request()!\n");
				// Check the namespace and obtain the respective sockaddr_in in host_tree and forward to outside hosts
				if(handle_receive_itc_msg_forward() == false)
				{
					printf("\tDEBUG: itcgw - Failed to handle_receive_itc_msg_forward()!\n");
					exit(EXIT_FAILURE);
				}
			
			}

			/* Check if any incoming data on all accepted fd */
			for(iter = itcgw_inst.used_list->head; iter != NULL; iter = iter->next)
			{
				host = (struct itc_host_info *)iter->p_data;
				if(host->sockfd != -1 && FD_ISSET(host->sockfd, &fd_set))
				{
					if(host->state == HOST_CONNECTING)
					{
						/* Partner host has replied us get_namespace request */
						/* If fd is triggered but nothing received -> partner host disconnected */
						printf("\tDEBUG: itcgw - HOST_CONNECTING!\n");
						if(handle_receive_get_namespace(host) == false)
						{
							exit(EXIT_FAILURE);
						}
					} else if(host->state == HOST_CONNECTED)
					{
						/* Partner host is sending us some requests? check it out! */
						/* If fd is triggered but nothing received -> partner host disconnected */
						printf("\tDEBUG: itcgw - HOST_CONNECTED!\n");
						if(handle_receive_requests(host) == false)
						{
							exit(EXIT_FAILURE);
						}
					} else
					{
						printf("\tDEBUG: itcgw - Host with namespace \"%s\" in wrong state, state = %d!\n", host->namespace, host->state);
						exit(EXIT_FAILURE);
					}
				} // Need handle for zombie host that disconnected before sending us get_namespace reply, so it's always in HOST_CONNECTING state
			}
		}

		counter++;

	}
	
	exit(EXIT_SUCCESS);

}







/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void itcgw_init(void)
{
	/* Ignore SIGPIPE signal, because by any reason, any socket/fd that was connected
	** to this process is corrupted a SIGPIPE will be sent to this process and causes it crash.
	** By ignoring this signal, itcgw can be run as a daemon (run on background) */
	signal(SIGPIPE, SIG_IGN);
	// Call our own exit_handler to release all resources if receiving any of below signals
	signal(SIGSEGV, itcgw_sig_handler);
	signal(SIGILL, itcgw_sig_handler); // When CPU executed an instruction it did not understand
	signal(SIGABRT, itcgw_sig_handler);
	signal(SIGFPE, itcgw_sig_handler); // Reports a fatal arithmetic error, for example divide-by-zero
	signal(SIGTERM, itcgw_sig_handler);
	signal(SIGINT, itcgw_sig_handler);
}

static void itcgw_sig_handler(int signo)
{
	// Call our own exit_handler
	itcgw_exit_handler();

	// After clean up, resume raising the suppressed signal
	signal(signo, SIG_DFL); // Inform kernel does fault exit_handler for this kind of signal
	raise(signo);
}

static void itcgw_exit_handler(void)
{
	printf("\tDEBUG: itcgw_exit_handler - ITCGW is terminated, calling exit handler...\n");


}

static bool setup_broadcast_fd(void)
{
	itcgw_inst.udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(itcgw_inst.udp_fd < 0)
	{
		printf("\tDEBUG: setup_broadcast_fd - Failed to get socket(), errno = %d!\n", errno);
		return false;
	}

	int broadcast_opt = 1;
	int res = setsockopt(itcgw_inst.udp_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tDEBUG: setup_broadcast_fd - Failed to set sockopt SO_BROADCAST, errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	res = setsockopt(itcgw_inst.udp_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tDEBUG: setup_broadcast_fd - Failed to set sockopt SO_REUSEADDR, errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	/* This is our host's address configuration */
	struct sockaddr_in ipaddr;
	memset(&ipaddr, 0, sizeof(struct sockaddr_in));
	ipaddr.sin_family = AF_INET;
	ipaddr.sin_addr.s_addr = INADDR_ANY;
	ipaddr.sin_port = htons(ITCGW_BROADCAST_PORT);

	int size = sizeof(struct sockaddr_in);
	res = bind(itcgw_inst.udp_fd, (struct sockaddr *)((void *)&ipaddr), size);
	if(res < 0)
	{
		printf("\tDEBUG: setup_broadcast_fd - Failed to bind, errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	itcgw_inst.udp_addr = ipaddr;

	printf("\tDEBUG: setup_broadcast_fd - Setup broadcast channel successfully on %s:%d\n", inet_ntoa(ipaddr.sin_addr), ntohs(ipaddr.sin_port));
	return true;
}

static bool setup_listening_fd(void)
{
	itcgw_inst.tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(itcgw_inst.tcp_fd < 0)
	{
		printf("\tDEBUG: setup_listening_fd - Failed to get socket(), errno = %d!\n", errno);
		return false;
	}

	int listening_opt = 1;
	int res = setsockopt(itcgw_inst.tcp_fd, SOL_SOCKET, SO_REUSEADDR, &listening_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tDEBUG: setup_listening_fd - Failed to set sockopt SO_REUSEADDR, errno = %d!\n", errno);
		close(itcgw_inst.tcp_fd);
		return false;
	}

	struct ifreq ifrq;
	memset(&ifrq, 0, sizeof(struct ifreq));
	int size = strlen(ITCGW_NETWORK_INTERFACE_ETH0) + 1;
	memcpy(&(ifrq.ifr_ifrn.ifrn_name), ITCGW_NETWORK_INTERFACE_ETH0, size);
	if(ioctl(itcgw_inst.tcp_fd, SIOCGIFADDR, (caddr_t)&ifrq, sizeof(ifrq)) == -1)
	{
		printf("\tDEBUG: setup_listening_fd - Failed to ioctl to get listening's ip address from %s, errno = %d!\n", ITCGW_NETWORK_INTERFACE_ETH0, errno);
		close(itcgw_inst.tcp_fd);
		return false;
	}

	struct sockaddr_in ipaddr;
	memset(&ipaddr, 0, sizeof(struct sockaddr_in));
	size = sizeof(struct sockaddr_in);
	memcpy(&ipaddr, &(ifrq.ifr_ifru.ifru_addr), size);
	ipaddr.sin_port = htons(ITCGW_LISTENING_PORT);

	res = bind(itcgw_inst.tcp_fd, (struct sockaddr *)&ipaddr, size);
	if(res < 0)
	{
		printf("\tDEBUG: setup_listening_fd - Failed to bind, errno = %d!\n", errno);
		close(itcgw_inst.tcp_fd);
		return false;
	}

	itcgw_inst.tcp_addr = ipaddr;

	printf("\tDEBUG: setup_listening_fd - Setup listening channel successfully on %s:%d\n", inet_ntoa(ipaddr.sin_addr), ntohs(ipaddr.sin_port));
	return true;
}

static bool handle_receive_greeting_message(int udpfd)
{
	struct sockaddr_in parter_broadcast_addr;
	char rx_buff[ITCGW_ETHERNET_PACKET_SIZE];
	socklen_t length = sizeof(struct sockaddr_in);

	memset(&parter_broadcast_addr, 0, length);

	int res = recvfrom(udpfd, rx_buff, ITCGW_ETHERNET_PACKET_SIZE, 0, (struct sockaddr *)((void *)&parter_broadcast_addr), &length);
	if(res < 0)
	{
		if(errno != EINTR)
		{
			printf("\tDEBUG: handle_receive_greeting_message - Received a malformed packet, errno = %d!\n", errno);
		} else
		{
			printf("\tDEBUG: handle_receive_greeting_message - Receiving message was interrupted, continue receiving!\n");
		}
		return false;
	}

	rx_buff[res] = '\0';

	char tcp_ip[100];
	int tcp_port;
	/* inet_ntoa(server_addr), ITCGW_LISTENING_PORT",  */
	res = sscanf(rx_buff, "ITC Gateway Broadcast Greeting Message from tcp://%s:%d/", tcp_ip, &tcp_port);
	struct sockaddr_in ipaddr;
	memset(&ipaddr, 0, sizeof(ipaddr));
	ipaddr.sin_family = AF_INET;
	inet_pton(AF_INET, tcp_ip, &(ipaddr.sin_addr));
	ipaddr.sin_port = tcp_port;

	printf("\tDEBUG: handle_receive_greeting_message - Received a greeting message from tcp://%s:%d/", tcp_ip, ntohs(tcp_port));

	// How to avoid receiving broadcast greeting message from already connected host??? 

	int new_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(new_fd < 0)
	{
		printf("\tDEBUG: handle_receive_greeting_message - Failed to create socket, errno = %d!\n", errno);
		return false;
	}

	res = connect(new_fd, (struct sockaddr*)&ipaddr, sizeof(ipaddr));
	if(res < 0)
	{
		printf("\tDEBUG: handle_receive_greeting_message - Failed to connect, errno = %d!\n", errno);
		return false;
	}

	struct itc_host_info *host;
	rc->flags = ITC_OK;
	host = q_dequeue(rc, itcgw_inst.free_list);
	if(rc->flags != ITC_OK)
	{
		printf("\tDEBUG: handle_receive_greeting_message - Failed to q_dequeue host from free_list, rc = %d!\n", rc->flags);
		return false;
	}

	if(host != NULL)
	{
		host->sockfd = new_fd;
		host->state = HOST_CONNECTING;

		q_enqueue(rc, itcgw_inst.used_list, host);
		if(rc->flags != ITC_OK)
		{
			printf("\tDEBUG: handle_receive_greeting_message - Failed to q_enqueue host to used_list, rc = %d!\n", rc->flags);
			return false;
		}
	}
	
	size_t msg_len = sizeof(struct itcgw_header) + sizeof(struct itcgw_get_namespace_request) + strlen(itcgw_inst.namespace) + 1;
	struct itcgw_msg *getns_req = malloc(msg_len);
	if(getns_req == NULL)
	{
		printf("\tDEBUG: handle_receive_greeting_message - Failed to malloc connect request message!\n");
		// REMOVE FROM USED_LIST
		return false;
	}

	uint32_t payload_length = sizeof(struct itcgw_get_namespace_request) + strlen(itcgw_inst.namespace) + 1;
	getns_req->header.sender 					= htonl((uint32_t)getpid());
	getns_req->header.receiver 					= htonl(111);
	getns_req->header.protRev 					= htonl(ITCGW_PROT_REV_1);
	getns_req->header.msgno 					= htonl(ITCGW_GET_NAMESPACE_REQUEST);
	getns_req->header.payloadLen 					= htonl(payload_length);

	getns_req->payload.itcgw_get_namespace_request.errorcode	= htonl(ITCGW_STATUS_OK);
	strcpy(getns_req->payload.itcgw_get_namespace_request.namespace, itcgw_inst.namespace);

	res = send(new_fd, getns_req, msg_len, 0);
	if(res < 0)
	{
		printf("\tDEBUG: handle_receive_greeting_message - Failed to send ITCGW_GET_NAMESPACE_REQUEST, errno = %d!\n", errno);
		// REMOVE FROM USED_LIST
		return false;
	}

	free(getns_req);
	return true;
}

static bool handle_accept_incoming_connection(int tcpfd)
{
	struct sockaddr_in new_addr = { 0 };
	unsigned int addr_size = sizeof(struct sockaddr_in);

	printf("\tDEBUG: handle_accept_incoming_connection - Accepting connection from host on TCP listening FD!\n");

	int new_fd = accept(tcpfd, (struct sockaddr *)((void *)&new_addr), &addr_size);
	if(new_fd < 0)
	{
		if(errno == EINTR)
		{
			printf("\tDEBUG: handle_accept_incoming_connection - Accepting connection was interrupted, just continue!\n");
			continue;
		} else
		{
			printf("\tDEBUG: handle_accept_incoming_connection - Accepting connection was destroyed!\n");
			return false;
		}
	}

	struct itc_host_info *host;
	rc->flags = ITC_OK;
	host = q_dequeue(rc, itcgw_inst.free_list);
	if(rc->flags != ITC_OK)
	{
		printf("\tDEBUG: handle_accept_incoming_connection - Failed to q_dequeue host from free_list, rc = %d!\n", rc->flags);
		return false;
	}

	if(host != NULL)
	{
		host->sockfd = new_fd;
		host->state = HOST_CONNECTED;

		q_enqueue(rc, itcgw_inst.used_list, host);
		if(rc->flags != ITC_OK)
		{
			printf("\tDEBUG: handle_accept_incoming_connection - Failed to q_enqueue host to used_list, rc = %d!\n", rc->flags);
			return false;
		}
	}

	return true;
}

static int recv_data(int sockfd, void *rx_buff, int nr_bytes_to_read)
{
	int length = 0;
	int read_count = 0;

	do
	{
		length = recv(sockfd, (char *)rx_buff + read_count, nr_bytes_to_read, 0);
		if(length <= 0)
		{
			return length;
		}

		read_count += length;
		nr_bytes_to_read = nr_bytes_to_read - length;
	} while(nr_bytes_to_read > 0)

	return read_count;
}

static bool handle_receive_itc_msg_forward(void)
{
	union itc_msg *msg;

	msg = itc_receive(ITC_NO_WAIT);

	if(msg == NULL)
	{
		printf("\tDEBUG: handle_receive_itc_msg_forward - Fatal error, itcgw received a NULL itc_msg!\n");
		return false;
	}

	if(msg->msgno != ITC_FWD_DATA_TO_ITCGWS)
	{
		printf("\tDEBUG: handle_receive_itc_msg_forward - Received invalid message msgno = 0x%08x\n",, msg->msgno);
		itc_free(&msg);
		return false;
	}

	char *to_namespace = msg->itc_fwd_data_to_itcgws.to_namespace;

	struct itc_host_info * to_host;

	to_host = locate_host_in_tree(to_namespace);
	
	if(to_host->state != HOST_CONNECTED)
	{
		printf("\tDEBUG: handle_receive_itc_msg_forward - Host with namespace \"%s\" not connected yet!\n", to_namespace);
		itc_free(&msg);
	}

	size_t msg_len = offsetof(struct itcgw_msg, payload) + offsetof(struct itcgw_itc_data_fwd, payload) + msg->itc_fwd_data_to_itcgws.payload_length;
	struct itcgw_msg *itc_fwd = malloc(msg_len);
	if(itc_fwd == NULL)
	{
		printf("\tDEBUG: handle_receive_itc_msg_forward - Failed to malloc forwarded itc messages!\n");
		itc_free(&msg);
		return false;
	}

	uint32_t payload_length = offsetof(struct itcgw_itc_data_fwd, payload) + msg->itc_fwd_data_to_itcgws.payload_length;
	itc_fwd->header.sender 					= htonl((uint32_t)getpid());
	itc_fwd->header.receiver 				= htonl(111);
	itc_fwd->header.protRev 				= htonl(ITCGW_PROT_REV_1);
	itc_fwd->header.msgno 					= htonl(ITCGW_ITC_DATA_FWD);
	itc_fwd->header.payloadLen 				= htonl(payload_length);

	itc_fwd->payload.itcgw_itc_data_fwd.errorcode		= htonl(ITCGW_STATUS_OK);
	itc_fwd->payload.itcgw_itc_data_fwd.payload_length 	= htonl(msg->itc_fwd_data_to_itcgws.payload_length);
	memcpy(itc_fwd->payload.itcgw_itc_data_fwd.payload, msg->itc_fwd_data_to_itcgws.payload, msg->itc_fwd_data_to_itcgws.payload_length);

	res = send(to_host->sockfd, itc_fwd, msg_len, 0);
	if(res < 0)
	{
		printf("\tDEBUG: handle_receive_itc_msg_forward - Failed to send ITCGW_ITC_DATA_FWD, errno = %d!\n", errno);
		itc_free(&msg);
		return false;
	}

	free(itc_fwd);
	itc_free(&msg);
	return true;
}

static struct itc_host_info *locate_host_in_tree(char *namespace)
{
	struct itc_host_info **iter;

	iter = tfind(namespace, &itcgw_inst.host_tree, namespace_cmpfunc);
	if(iter == NULL)
	{
		printf("\tDEBUG: locate_host_in_tree - Contact for host with namespace \"%s\" not availble in itcgw which maybe not online!\n", namespace);
		return NULL;
	}

	printf("\tDEBUG: locate_host_in_tree - Contact for host with namespace \"%s\" is found!\n", namespace);
	return *iter;
}

static int namespace_cmpfunc(const void *pa, const void *pb)
{
	const char *namespace = pa;
	const struct itc_host_info *host = pb;

	return strcmp(name, host->namespace);
}

static bool handle_receive_get_namespace(struct itc_host_info *from_host)
{
	from_host->tcp_addr
}










