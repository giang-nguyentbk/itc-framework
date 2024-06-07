#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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

#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>

#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "itc.h"
#include "itc_impl.h"
#include "itc_proto.h"
#include "itc_gw_proto.h"

#include "traceIf.h"

/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
union itc_msg {
	uint32_t					msgno;

	struct itc_fwd_data_to_itcgws			itc_fwd_data_to_itcgws;
	struct itcgw_udp_add_peer			itcgw_udp_add_peer;
	struct itcgw_udp_rmv_peer			itcgw_udp_rmv_peer;
	struct itc_get_namespace_request		itc_get_namespace_request;
	struct itc_get_namespace_reply			itc_get_namespace_reply;
	struct itc_locate_mbox_from_itcgws_request	itc_locate_mbox_from_itcgws_request;
	struct itc_locate_mbox_from_itcgws_reply	itc_locate_mbox_from_itcgws_reply;
};

struct udp_peer_info {
	char			addr[ITC_MAX_NAME_LENGTH]; // In form, for example, "tcp://192.168.0.2:8888/"
};

struct tcp_peer_info {
	char			addr[ITC_MAX_NAME_LENGTH]; // In form, for example, "tcp://192.168.0.2:8888/"
	int			fd;
	char			namespace[ITC_MAX_NAME_LENGTH];
};

struct itcgw_instance {
	/* Our own host stuff's part */
	char					namespace[ITC_MAX_NAME_LENGTH];
	itc_mbox_id_t				itccoord_mbox_id;

	/* UDP part */
	int					udp_fd;
	struct sockaddr_in			udp_addr;
	struct sockaddr_in			udp_peer_addr;
	int					udp_broadcast_timer_fd;
	char					udp_broadtcast_msg[ITC_MAX_NAME_LENGTH*2];
	struct udp_peer_info			udp_peers[ITC_GATEWAY_MAX_PEERS];
	void					*udp_tree;
	int					udp_mbox_fd;
	itc_mbox_id_t				udp_mbox_id;

	/* TCP Server part */
	int					tcp_server_fd;
	struct sockaddr_in			tcp_server_addr;
	pthread_t				tcp_server_tid;
	pthread_mutex_t				tcp_server_mtx;
	pthread_key_t				tcp_server_destruct_key;
	int					tcp_server_mbox_fd;
	itc_mbox_id_t				tcp_server_mbox_id;
	struct tcp_peer_info			tcp_server_peers[ITC_GATEWAY_MAX_PEERS];
	void					*tcp_server_tree;

	/* TCP Client part */
	pthread_t				tcp_client_tid;
	pthread_mutex_t				tcp_client_mtx;
	pthread_key_t				tcp_client_destruct_key;
	int					tcp_client_mbox_fd;
	itc_mbox_id_t				tcp_client_mbox_id;
	struct tcp_peer_info			tcp_client_peers[ITC_GATEWAY_MAX_PEERS];
	void					*tcp_client_tree;

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
static bool setup_log_file(void);
static bool setup_rc(void);
static bool setup_udp_mailbox(void);
static bool setup_udp_server(void);
static bool setup_udp_peer(void);
static bool setup_tcp_server(void);
static bool setup_broadcast_timer(void);
static bool check_broadcast_timer(time_t interval);
static struct in_addr get_ip_address_from_network_interface(int sockfd, char *interface);
static bool create_broadcast_message(void);
static bool handle_receive_broadcast_msg(int sockfd);
static int compare_peer_udp_tree(const void *pa, const void *pb);
static int compare_addr_udp_tree(const void *pa, const void *pb);
static int compare_peer_tcp_tree(const void *pa, const void *pb);
static int compare_addr_tcp_tree(const void *pa, const void *pb);
static int compare_sockfd_tcp_tree(const void *pa, const void *pb);
static int compare_namespace_tcp_tree(const void *pa, const void *pb);
static void do_nothing(void *tree_node_data);
static bool setup_tcp_threads(void);
static void tcp_server_thread_destructor(void* data);
static void tcp_client_thread_destructor(void* data);
static bool start_tcp_client_thread(void);
static bool start_tcp_server_thread(void);
static void* tcp_server_loop(void *data);
static void* tcp_client_loop(void *data);
static bool setup_tcp_server_mailbox(void);
static bool setup_tcp_server_peer(void);
static bool setup_tcp_client_mailbox(void);
static bool setup_tcp_client_peer(void);
static bool handle_accept_new_connection(int sockfd);
static bool handle_receive_tcp_packet_at_server(int sockfd);
static bool delete_tcp_peer_resource(int sockfd);
static bool handle_receive_itcmsg_at_client(int sockfd);
static bool handle_receive_tcp_packet_at_client(int sockfd);
static bool handle_tcp_client_add_peer(char *addr, char *namespace);
static bool handle_tcp_client_rmv_peer(int sockfd);
static bool handle_fwd_data_out(union itc_msg *msg);
static bool handle_locate_mbox_request(union itc_msg *msg);
static bool handle_receive_itcmsg_at_udp(int sockfd);
static bool handle_udp_rmv_peer(char *addr);
static int recv_data(int sockfd, void *rx_buff, int nr_bytes_to_read);
static bool handle_udp_get_namespace_request(itc_mbox_id_t mbox_id);
static bool handle_receive_data_fwd(int sockfd, struct itcgw_header *header);
static bool handle_receive_locate_mbox(int sockfd, struct itcgw_header *header);
static bool send_locate_mbox_reply(int sockfd, itc_mbox_id_t mbox_id);
static bool handle_receive_locate_mbox_reply(int sockfd, struct itcgw_header *header);
static bool itcgws_config(void);


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{
	itcgw_init();

	int opt = 0;
	bool is_daemon = false;
	strcpy(itcgw_inst.namespace, "");

	while((opt = getopt(argc, argv, "dn:")) != -1)
	{
		switch (opt)
		{
		case 'd':
			is_daemon = true;
			break;
		
		case 'n':
			if(strlen(optarg) > (ITC_MAX_NAME_LENGTH - 1))
			{
				printf("ERROR: Namespace too long, consider to short this namespace \"%s\"!\n", optarg);
				exit(EXIT_FAILURE);
			}

			strcpy(itcgw_inst.namespace, optarg);
			break;
		default:
			printf("ERROR: Usage:\t%s\t[-n namespace]\t[-d]\n", argv[0]);
			printf("Example:\t%s\t-n   \"/host_1/\"\t-d\n", argv[0]);
			printf("=> This will assign namespace \"/host_1/\" to our itcgws and start itcgws as a daemon!\n");
			exit(EXIT_FAILURE);
			break;
		}
	}

	if(strcmp(itcgw_inst.namespace, "") == 0)
	{
		printf("ERROR: Namespace was not provided!\n");
		exit(EXIT_FAILURE);
	}

	if(is_daemon)
	{
		printf(">>> Starting itcgws daemon...\n");

		if(!setup_log_file())
		{
			LOG_ERROR("Failed to setup log file for this itcgws daemon!\n");
			exit(EXIT_FAILURE);
		}

		if(daemon(1, 1))
		{
			LOG_ERROR("Failed to start itcgws as a daemon!\n");
			exit(EXIT_FAILURE);
		}

		LOG_INFO("Starting itcgws daemon...\n");
	} else
	{
		LOG_INFO("Starting itcgws, but not as a daemon...\n");
	}

	if(!itcgws_config())
	{
		LOG_ERROR("Failed to setup necessary modules for itcgw!\n");
		exit(EXIT_FAILURE);
	}

	// At normal termination we just clean up our resources by registration a exit_handler
	atexit(itcgw_exit_handler);

	int res;
	fd_set fdset;
	int max_fd = -1;
	while(1)
	{
		check_broadcast_timer(ITC_GATEWAY_BROADCAST_INTERVAL);

		FD_ZERO(&fdset);
		FD_SET(itcgw_inst.udp_fd, &fdset);
		max_fd = MAX_OF(itcgw_inst.udp_fd, max_fd);
		FD_SET(itcgw_inst.udp_broadcast_timer_fd, &fdset);
		max_fd = MAX_OF(itcgw_inst.udp_broadcast_timer_fd, max_fd);
		FD_SET(itcgw_inst.udp_mbox_fd, &fdset);
		max_fd = MAX_OF(itcgw_inst.udp_mbox_fd, max_fd);

		res = select(max_fd + 1, &fdset, NULL, NULL, NULL);
		if(res < 0)
		{
			LOG_ERROR("Failed to select() in UDP loop!\n");
			exit(EXIT_FAILURE);
		}

		if(FD_ISSET(itcgw_inst.udp_fd, &fdset))
		{
			if(handle_receive_broadcast_msg(itcgw_inst.udp_fd) == false)
			{
				LOG_ERROR("Failed to handle_receive_broadcast_msg()!\n");
				exit(EXIT_FAILURE);
			}
		}

		if(FD_ISSET(itcgw_inst.udp_mbox_fd, &fdset))
		{
			if(handle_receive_itcmsg_at_udp(itcgw_inst.udp_mbox_fd) == false)
			{
				LOG_ERROR("Failed to handle_receive_itcmsg_ast_udp()!\n");
				exit(EXIT_FAILURE);
			}
		}
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
	LOG_INFO("ITCGW is terminated with SIG = %d, calling exit handler...\n", signo);
	itcgw_exit_handler();

	// After clean up, resume raising the suppressed signal
	signal(signo, SIG_DFL); // Inform kernel does fault exit_handler for this kind of signal
	raise(signo);
}

static void itcgw_exit_handler(void)
{
	LOG_INFO("Closing file descriptors...\n");
	close(itcgw_inst.udp_fd);
	close(itcgw_inst.tcp_server_fd);
	close(itcgw_inst.udp_broadcast_timer_fd);

	LOG_INFO("Destroying UDP, TCP server and client trees...\n");
	tdestroy(itcgw_inst.udp_tree, do_nothing);
	tdestroy(itcgw_inst.tcp_server_tree, do_nothing);
	tdestroy(itcgw_inst.tcp_client_tree, do_nothing);

	LOG_INFO("Deleting UDP, TCP server, TCP client mailboxes...\n");
	if(itcgw_inst.udp_mbox_id != 0 || itcgw_inst.tcp_client_mbox_id != ITC_NO_MBOX_ID)
	{
		itc_delete_mailbox(itcgw_inst.udp_mbox_id);
	}

	int ret = pthread_cancel(itcgw_inst.tcp_server_tid);
	if(ret != 0)
	{
		LOG_ERROR("Failed to pthread_cancel server, error code = %d\n", ret);
	}

	ret = pthread_join(itcgw_inst.tcp_server_tid, NULL);
	if(ret != 0)
	{
		LOG_ERROR("Failed to pthread_join server, error code = %d\n", ret);
	}

	ret = pthread_cancel(itcgw_inst.tcp_client_tid);
	if(ret != 0)
	{
		LOG_ERROR("Failed to pthread_cancel client, error code = %d\n", ret);
	}

	ret = pthread_join(itcgw_inst.tcp_client_tid, NULL);
	if(ret != 0)
	{
		LOG_ERROR("Failed to pthread_join client, error code = %d\n", ret);
	}

	LOG_INFO("Exiting ITC system...\n");
	itc_exit();

	free(rc);
	LOG_INFO("ITCGW exit handler finished!\n");
}

static bool itcgws_config(void)
{
	if(!setup_rc())
	{
		free(rc);
		return false;
	} else if(!setup_udp_mailbox() || !setup_udp_server() || !setup_udp_peer() || !setup_broadcast_timer() || !setup_tcp_server() \
		|| !create_broadcast_message() || !setup_tcp_threads() || !start_tcp_server_thread() || !start_tcp_client_thread())
	{
		return false;
	}

	return true;
}

static bool setup_log_file(void)
{
	/* Setup a log file for our itcgws daemon */
	freopen(ITC_ITCGWS_LOGFILE, "a+", stdout);
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
			LOG_ERROR("Failed to malloc rc due to out of memory!\n");
                	return false;
		}	
	}
	rc->flags = ITC_OK;

	return true;
}

static bool setup_udp_mailbox(void)
{
	/* Create a mailbox named "itc_gw_udp" used for exchanging itc_msg between processes in our host (internal-side). */
	// Allocate 4 mailboxes, one is for udp thread, one is for tcp server thread, one is for tcp client thread, the other one is reserved
	if(itc_init(4, ITC_MALLOC, 0) == false)
	{
		LOG_ERROR("Failed to itc_init() by ITCGW!\n");
		return false;
	}

	itcgw_inst.udp_mbox_id = itc_create_mailbox(ITC_GATEWAY_MBOX_UDP_NAME, ITC_NO_NAMESPACE);
	if(itcgw_inst.udp_mbox_id == ITC_NO_MBOX_ID)
	{
		LOG_ERROR("Failed to create mailbox %s!\n", ITC_GATEWAY_MBOX_UDP_NAME);
		itc_exit();
		return false;
	}

	itcgw_inst.udp_mbox_fd = itc_get_fd(itcgw_inst.udp_mbox_id);
	LOG_INFO("Setup UDP mailbox \"%s\" successfully!\n", ITC_GATEWAY_MBOX_UDP_NAME);
	return true;
}

static bool setup_udp_server(void)
{
	itcgw_inst.udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(itcgw_inst.udp_fd < 0)
	{
		LOG_ERROR("Failed to create socket(), errno = %d!\n", errno);
		return false;
	}

	int broadcast_opt = 1;
	int res = setsockopt(itcgw_inst.udp_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		LOG_ERROR("Failed to setsockopt() SO_BROADCAST, errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	res = setsockopt(itcgw_inst.udp_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		LOG_ERROR("Failed to setsockopt() SO_REUSEADDR, errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	struct sockaddr_in myUDPaddr;
	size_t size = sizeof(struct sockaddr_in);
	memset(&myUDPaddr, 0, size);
	myUDPaddr.sin_family = AF_INET;
	myUDPaddr.sin_addr.s_addr = INADDR_ANY;
	myUDPaddr.sin_port = htons(ITC_GATEWAY_BROADCAST_PORT);

	res = bind(itcgw_inst.udp_fd, (struct sockaddr *)((void *)&myUDPaddr), size);
	if(res < 0)
	{
		LOG_ERROR("Failed to bind(), errno = %d!\n", errno);
		close(itcgw_inst.udp_fd);
		return false;
	}

	itcgw_inst.udp_addr = myUDPaddr;

	LOG_INFO("Setup my UDP successfully on %s:%d\n", inet_ntoa(myUDPaddr.sin_addr), ntohs(myUDPaddr.sin_port));
	return true;
}

static bool setup_udp_peer(void)
{
	/* This is address configuration of peer broadcast UDP */
	memset(&itcgw_inst.udp_peer_addr, 0, sizeof(struct sockaddr_in));
	itcgw_inst.udp_peer_addr.sin_family = AF_INET;
	itcgw_inst.udp_peer_addr.sin_port = htons((short)(ITC_GATEWAY_BROADCAST_PORT & 0xFFFF));
	itcgw_inst.udp_peer_addr.sin_addr.s_addr = INADDR_BROADCAST;

	for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		strcpy(itcgw_inst.udp_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING); 
	}

	LOG_INFO("Setup UDP peer successfully on %s:%d\n", inet_ntoa(itcgw_inst.udp_peer_addr.sin_addr), ITC_GATEWAY_BROADCAST_PORT);
	return true;
}

static bool setup_tcp_server(void)
{
	int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
	if(tcpfd < 0)
	{
		LOG_ERROR("Failed to get socket(), errno = %d!\n", errno);
		return false;
	}

	int listening_opt = 1;
	int res = setsockopt(tcpfd, SOL_SOCKET, SO_REUSEADDR, &listening_opt, sizeof(int));
	if(res < 0)
	{
		LOG_ERROR("Failed to set sockopt SO_REUSEADDR, errno = %d!\n", errno);
		close(tcpfd);
		return false;
	}

	memset(&itcgw_inst.tcp_server_addr, 0, sizeof(struct sockaddr_in));
	size_t size = sizeof(struct sockaddr_in);
	itcgw_inst.tcp_server_addr.sin_family = AF_INET;
	itcgw_inst.tcp_server_addr.sin_addr = get_ip_address_from_network_interface(tcpfd, ITC_GATEWAY_NET_INTERFACE_ETH0);
	itcgw_inst.tcp_server_addr.sin_port = htons(ITC_GATEWAY_TCP_LISTENING_PORT);

	res = bind(tcpfd, (struct sockaddr *)&itcgw_inst.tcp_server_addr, size);
	if(res < 0)
	{
		LOG_ERROR("Failed to bind, errno = %d!\n", errno);
		close(tcpfd);
		return false;
	}

	res = listen(tcpfd, ITC_GATEWAY_MAX_PEERS);
	if(res < 0)
	{
		LOG_ERROR("Failed to listen, errno = %d!\n", errno);
		close(tcpfd);
		return false;
	}

	itcgw_inst.tcp_server_fd = tcpfd;

	LOG_INFO("Setup TCP server successfully on %s:%d\n", inet_ntoa(itcgw_inst.tcp_server_addr.sin_addr), ntohs(itcgw_inst.tcp_server_addr.sin_port));
	return true;
}

static bool setup_broadcast_timer(void)
{
	itcgw_inst.udp_broadcast_timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
	if(itcgw_inst.udp_broadcast_timer_fd < 0)
	{
		LOG_ERROR("Failed to timerfd_create(), errno = %d!\n", errno);
		return false;
	}

	LOG_INFO("Broadcast timer fd %d created successfully!\n", itcgw_inst.udp_broadcast_timer_fd);
	return true;
}

static bool check_broadcast_timer(time_t interval)
{
	struct timespec now;
	struct itimerspec remaining_time;
	struct itimerspec its;

	int res = timerfd_gettime(itcgw_inst.udp_broadcast_timer_fd, &remaining_time);
	if(res < 0)
	{
		LOG_ERROR("Failed to timerfd_gettime(), errno = %d!\n", errno);
		return false;
	}

	LOG_INFO("Broadcast timer will expire in: %ld.%ld seconds!\n", remaining_time.it_value.tv_sec, remaining_time.it_value.tv_nsec / 1000000);

	clock_gettime(CLOCK_REALTIME, &now);
	if(remaining_time.it_value.tv_sec == 0 && remaining_time.it_value.tv_nsec == 0)
	{
		LOG_INFO("Reset timer %lds, send broadcasting message...!\n", interval);
		memset(&its, 0, sizeof(struct itimerspec));
		its.it_value.tv_sec = now.tv_sec + (time_t)interval;
		its.it_value.tv_nsec = now.tv_nsec;
		res = timerfd_settime(itcgw_inst.udp_broadcast_timer_fd, TFD_TIMER_ABSTIME, &its, NULL);
		if(res < 0)
		{
			LOG_ERROR("Failed to timerfd_settime(), errno = %d!\n", errno);
			return false;
		}

		/* 1. Broadcasting greeting messages: */
		res = sendto(itcgw_inst.udp_fd, itcgw_inst.udp_broadtcast_msg, strlen(itcgw_inst.udp_broadtcast_msg), 0, (struct sockaddr *)((void *)&itcgw_inst.udp_peer_addr), sizeof(struct sockaddr_in));
		if(res < 0)
		{
			LOG_ERROR("Failed to broadcast greeting message, errno = %d!\n", errno);
			return false;
		}
	}

	return true;
}

static struct in_addr get_ip_address_from_network_interface(int sockfd, char *interface)
{
	struct sockaddr_in sock_addr;
	struct ifreq ifrq;
	memset(&ifrq, 0, sizeof(struct ifreq));
	int size = strlen(interface) + 1;
	memcpy(&(ifrq.ifr_ifrn.ifrn_name), interface, size);

	/* Get IP address from network interface, such as: lo, eth0, eth1,... */
	size = sizeof(struct ifreq);
	int res = ioctl(sockfd, SIOCGIFADDR, (caddr_t)&ifrq, size);
	if(res < 0)
	{
		LOG_ERROR("Failed to ioctl to obtain IP address from %s, errno = %d!\n", interface, errno);
		return sock_addr.sin_addr;
	}

	size = sizeof(struct sockaddr_in);
	memcpy(&sock_addr, &(ifrq.ifr_ifru.ifru_addr), size);

	LOG_INFO("Retrieve address from network interface \"%s\" -> tcp://%s:%d\n", interface, inet_ntoa(sock_addr.sin_addr), sock_addr.sin_port);
	return sock_addr.sin_addr;
}

static bool create_broadcast_message(void)
{
	snprintf((char *)itcgw_inst.udp_broadtcast_msg, ITC_MAX_NAME_LENGTH*2, "Broadcast Message: ITCGW from host <%s> listening on tcp://%s:%hu/", itcgw_inst.namespace, inet_ntoa(itcgw_inst.tcp_server_addr.sin_addr), ntohs(itcgw_inst.tcp_server_addr.sin_port));

	LOG_INFO("Broadcasting message created successfully -> \"%s\"\n", itcgw_inst.udp_broadtcast_msg);
	return true;
}

static bool handle_receive_broadcast_msg(int sockfd)
{
	struct sockaddr_in m_peerUDPaddr;
	char rx_buff[ITC_GATEWAY_ETH_PACKET_SIZE];
	socklen_t length = sizeof(struct sockaddr_in);

	memset(&m_peerUDPaddr, 0, length);

	int res = recvfrom(sockfd, rx_buff, ITC_GATEWAY_ETH_PACKET_SIZE, 0, (struct sockaddr *)((void *)&m_peerUDPaddr), &length);
	if(res < 0)
	{
		if(errno != EINTR)
		{
			LOG_ERROR("Failed to recvfrom(), errno = %d!\n", errno);
		} else
		{
			LOG_ERROR("Receiving message was interrupted, continue receiving!\n");
		}
		return false;
	}

	rx_buff[res] = '\0';

	char tcp_ip[25];
	char namespace[ITC_MAX_NAME_LENGTH];
	uint16_t tcp_port;
	/* Instead of format string "%s" as usual, we must use "%[^:]" meaning read to string tcp_ip until character ':'. */
	res = sscanf(rx_buff, "Broadcast Message: ITCGW from host <%[^>]> listening on tcp://%[^:]:%hu/", namespace, tcp_ip, &tcp_port);
	LOG_INFO("Received a greeting message from hostname <%s> on tcp://%s:%hu/\n", namespace, tcp_ip, tcp_port);


	struct udp_peer_info **iter;
	char m_addr[40];
	snprintf(m_addr, 40, "tcp://%s:%hu/", tcp_ip, tcp_port);
	iter = tfind(m_addr, &itcgw_inst.udp_tree, compare_addr_udp_tree);

	if(iter != NULL)
	{
		/* Already added in tree */
		LOG_INFO("Already connected, ignore broadcasting message from this peer!\n");
		return true;
	} else
	{
		int i = 0;
		for(; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(strcmp(itcgw_inst.udp_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING) == 0)
			{
				/* Allocate a slot for this new connection */
				LOG_INFO("Adding new TCP peer connection successfully from tcp://%s:%hu/\n", tcp_ip, tcp_port);
				strcpy(itcgw_inst.udp_peers[i].addr, m_addr);
				tsearch(&itcgw_inst.udp_peers[i], &itcgw_inst.udp_tree, compare_peer_udp_tree);
				break;
			}
		}

		if(i == ITC_GATEWAY_MAX_PEERS)
		{
			LOG_ERROR("No more than %d peers is accepted!\n", ITC_GATEWAY_MAX_PEERS);
			return false;
		}
	}

	/* Notify TCP client about adding new peer */
	union itc_msg *req;
	req = itc_alloc(offsetof(struct itcgw_udp_add_peer, namespace) + strlen(namespace) + 1, ITCGW_UDP_ADD_PEER);

	strcpy(req->itcgw_udp_add_peer.addr, m_addr);
	strcpy(req->itcgw_udp_add_peer.namespace, namespace);

	if(itc_send(&req, itcgw_inst.tcp_client_mbox_id, ITC_MY_MBOX_ID, NULL) == false)
	{
		LOG_ERROR("Failed to send ITCGW_UDP_ADD_PEER to mailbox %s!\n", ITC_GATEWAY_MBOX_TCP_CLI_NAME);
		itc_free(&req);
		return false;
	}

	LOG_INFO("Sent ITCGW_UDP_ADD_PEER to mailbox \"%s\" successfully!\n", ITC_GATEWAY_MBOX_TCP_CLI_NAME);
	
	/* 1. Force broadcasting greeting messages */
	res = sendto(itcgw_inst.udp_fd, itcgw_inst.udp_broadtcast_msg, strlen(itcgw_inst.udp_broadtcast_msg), 0, (struct sockaddr *)((void *)&itcgw_inst.udp_peer_addr), sizeof(struct sockaddr_in));
	if(res < 0)
	{
		LOG_ERROR("Failed to force broadcasting greeting message, errno = %d!\n", errno);
		return false;
	}

	return true;
}

static int compare_peer_udp_tree(const void *pa, const void *pb)
{
	const struct udp_peer_info *peer_a = pa;
	const struct udp_peer_info *peer_b = pb;
	
	return strcmp(peer_a->addr, peer_b->addr);
}

static int compare_addr_udp_tree(const void *pa, const void *pb)
{
	const char *addr = pa;
	const struct udp_peer_info *peer = pb;
	
	return strcmp(addr, peer->addr);
}

static int compare_peer_tcp_tree(const void *pa, const void *pb)
{
	const struct tcp_peer_info *peer_a = pa;
	const struct tcp_peer_info *peer_b = pb;
	
	return strcmp(peer_a->addr, peer_b->addr);
}

static int compare_addr_tcp_tree(const void *pa, const void *pb)
{
	const char *addr = pa;
	const struct tcp_peer_info *peer = pb;
	
	return strcmp(addr, peer->addr);
}

static int compare_sockfd_tcp_tree(const void *pa, const void *pb)
{
	const int *sockfd = pa;
	const struct tcp_peer_info *peer = pb;
	
	if(*sockfd == peer->fd)
	{
		return 0;
	} else if(*sockfd > peer->fd)
	{
		return 1;
	} else
	{
		return -1;
	}
}

static int compare_namespace_tcp_tree(const void *pa, const void *pb)
{
	const char *namespace = pa;
	const struct tcp_peer_info *peer = pb;
	
	return strcmp(namespace, peer->namespace);
}

static void do_nothing(void *tree_node_data)
{
	(void)tree_node_data;
}

static bool setup_tcp_threads(void)
{
	int res = pthread_key_create(&itcgw_inst.tcp_server_destruct_key, tcp_server_thread_destructor);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_key_create server, error code = %d\n", res);
		return false;
	}

	res = pthread_mutex_init(&itcgw_inst.tcp_server_mtx, NULL);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_mutex_init server, error code = %d\n", res);
		pthread_key_delete(itcgw_inst.tcp_server_destruct_key);
		return false;
	}

	res = pthread_key_create(&itcgw_inst.tcp_client_destruct_key, tcp_client_thread_destructor);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_key_create client, error code = %d\n", res);
		pthread_key_delete(itcgw_inst.tcp_server_destruct_key);
		pthread_mutex_destroy(&itcgw_inst.tcp_server_mtx);
		return false;
	}

	res = pthread_mutex_init(&itcgw_inst.tcp_client_mtx, NULL);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_mutex_init client, error code = %d\n", res);
		pthread_key_delete(itcgw_inst.tcp_server_destruct_key);
		pthread_mutex_destroy(&itcgw_inst.tcp_server_mtx);
		pthread_key_delete(itcgw_inst.tcp_client_destruct_key);
		return false;
	}

	return true;
}

static void tcp_server_thread_destructor(void* data)
{
	(void)data;

	LOG_INFO("Calling tcp server thread destructor...\n");

	if(itcgw_inst.tcp_server_mbox_id != 0 || itcgw_inst.tcp_server_mbox_id != ITC_NO_MBOX_ID)
	{
		LOG_INFO("Deleting tcp server mailbox...\n");
		itc_delete_mailbox(itcgw_inst.tcp_server_mbox_id);
	}
}

static bool start_tcp_server_thread(void)
{
	MUTEX_LOCK(&itcgw_inst.tcp_server_mtx);
	int res = pthread_create(&itcgw_inst.tcp_server_tid, NULL, tcp_server_loop, NULL);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_create, error code = %d\n", res);
		return false;
	}
	MUTEX_LOCK(&itcgw_inst.tcp_server_mtx); // Wait until tcp_server_thread finishes their initialization
	MUTEX_UNLOCK(&itcgw_inst.tcp_server_mtx);

	return true;
}

static void* tcp_server_loop(void *data)
{
	(void)data;

	if(prctl(PR_SET_NAME, "itc_gw_tcp_server", 0, 0, 0) == -1)
	{
		// ERROR trace is needed here
		LOG_ERROR("Failed to prctl() TCP server loop!\n");
		return NULL;
	}

	if(!setup_tcp_server_mailbox() || !setup_tcp_server_peer())
	{
		LOG_ERROR("Failed to setup_tcp_server_mailbox!\n");
		return NULL;
	}

	LOG_INFO("Starting tcp server loop...\n");

	int res = pthread_setspecific(itcgw_inst.tcp_server_destruct_key, (void*)(unsigned long)itcgw_inst.tcp_server_mbox_id);
	if(res != 0)
	{
		// ERROR trace is needed here
		LOG_ERROR("Failed to pthread_setspecific, error code = %d\n", res);
		return NULL;
	}

	MUTEX_UNLOCK(&itcgw_inst.tcp_server_mtx); // Done tcp_server_thread initialization, wake udp_thread up!

	fd_set fdset;
	int max_fd = -1;
	while(1)
	{
		FD_ZERO(&fdset);
		FD_SET(itcgw_inst.tcp_server_fd, &fdset); // If some client want to connect() to our host, it will trigger this fd -> handle_accept_new_connection()
		max_fd = MAX_OF(itcgw_inst.tcp_server_fd, max_fd);

		// If receiving some itc_message which is wrapped as ethernet packages, forward them to our internal mailboxes -> handle_receive_tcp_packet_at_server()
		for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(itcgw_inst.tcp_server_peers[i].fd != -1)
			{
				FD_SET(itcgw_inst.tcp_server_peers[i].fd, &fdset);
				max_fd = MAX_OF(itcgw_inst.tcp_server_peers[i].fd, max_fd);
			}
		}

		res = select(max_fd + 1, &fdset, NULL, NULL, NULL);
		if(res < 0)
		{
			LOG_ERROR("Failed to select() in TCP server loop!\n");
			return NULL;
		}

		if(FD_ISSET(itcgw_inst.tcp_server_fd, &fdset))
		{
			if(handle_accept_new_connection(itcgw_inst.tcp_server_fd) == false)
			{
				LOG_ERROR("Failed to handle_accept_new_connection()!\n");
				return NULL;
			}
		}

		for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(itcgw_inst.tcp_server_peers[i].fd != -1 && FD_ISSET(itcgw_inst.tcp_server_peers[i].fd, &fdset))
			{
				/* Note that: if we receiving nothing though this fd has been triggered, that means our client has been disconnected,
				remove peer from list and notify UDP mailbox so that they can remove peer from udp_list as well */
				if(handle_receive_tcp_packet_at_server(itcgw_inst.tcp_server_peers[i].fd) == false)
				{
					LOG_ERROR("Failed to handle_receive_tcp_packet_at_server()!\n");
					return NULL;
				}
			}
		}
	}

	return NULL;
}

static bool setup_tcp_server_mailbox(void)
{
	itcgw_inst.tcp_server_mbox_id = itc_create_mailbox(ITC_GATEWAY_MBOX_TCP_SER_NAME, ITC_NO_NAMESPACE);
	if(itcgw_inst.tcp_server_mbox_id == ITC_NO_MBOX_ID)
	{
		LOG_ERROR("Failed to create mailbox %s\n", ITC_GATEWAY_MBOX_TCP_SER_NAME);
		return false;
	}

	itcgw_inst.tcp_server_mbox_fd = itc_get_fd(itcgw_inst.tcp_server_mbox_id);
	LOG_INFO("Create TCP server mailbox \"%s\" successfully!\n", ITC_GATEWAY_MBOX_TCP_SER_NAME);
	return true;
}

static bool setup_tcp_server_peer(void)
{
	for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		itcgw_inst.tcp_server_peers[i].fd = -1;
		strcpy(itcgw_inst.tcp_server_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING);
	}

	return true;
}

static bool handle_accept_new_connection(int sockfd)
{
	struct sockaddr_in new_addr;
	unsigned int addr_size = sizeof(struct sockaddr_in);
	memset(&new_addr, 0, addr_size);

	int new_fd = accept(sockfd, (struct sockaddr *)&new_addr, (socklen_t*)&addr_size);
	if(new_fd < 0)
	{
		if(errno == EINTR)
		{
			LOG_ABN("Accepting connection was interrupted, just ignore it!\n");
			return true;
		} else
		{
			LOG_ERROR("Accepting connection was destroyed!\n");
			return false;
		}
	}

	/* Why the peer port here is not 22222, this is because after calling connect(), kernel will choose an ephemeral port (or probably a source IP address if no more port available) to connect to our peer.
	Apart from that, port 22222 of our peer is listenning port, not the port to send out data. Similarly to us, our port 22223 is a listening, not a sending port.
	Which port to send data is chosen by kernel at the time we call connect() to a peer */
	// LOG_INFO("Accepting connection from tcp://%s:%hu/\n", inet_ntoa(new_addr.sin_addr), ntohs(new_addr.sin_port));

	struct tcp_peer_info **iter;
	char addr[30];
	LOG_INFO("Receiving new connection from a peer client tcp://%s:%hu/\n", inet_ntoa(new_addr.sin_addr), ntohs(new_addr.sin_port));
	snprintf(addr, 30, "tcp://%s:%hu/", inet_ntoa(new_addr.sin_addr), ITC_GATEWAY_TCP_LISTENING_PORT);

	iter = tfind(addr, &itcgw_inst.tcp_server_tree, compare_addr_tcp_tree);
	if(iter != NULL)
	{
		/* Already added in tree */
		LOG_ABN("Already connected, ignore connect() from this peer!\n");
	} else
	{
		int i = 0;
		for(; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(itcgw_inst.tcp_server_peers[i].fd == -1)
			{
				LOG_INFO("Accepting new tcp connection from %s\n", addr);
				strcpy(itcgw_inst.tcp_server_peers[i].addr, addr);
				itcgw_inst.tcp_server_peers[i].fd = new_fd;
				tsearch(&itcgw_inst.tcp_server_peers[i], &itcgw_inst.tcp_server_tree, compare_peer_tcp_tree);
				break;
			}
		}

		if(i == ITC_GATEWAY_MAX_PEERS)
		{
			LOG_ERROR("No more than %d peers is accepted!\n", ITC_GATEWAY_MAX_PEERS);
			return false;
		}
	}

	return true;
}

static bool handle_receive_tcp_packet_at_server(int sockfd)
{
	struct itcgw_header *header;
	int header_size = sizeof(struct itcgw_header);
	char rxbuff[header_size];
	int size = 0;

	size = recv_data(sockfd, rxbuff, header_size);

	if(size == 0)
	{
		LOG_INFO("Peer from this socket fd %d disconnected, remove it from server list!\n", sockfd);
		if(!delete_tcp_peer_resource(sockfd))
		{
			LOG_ERROR("Failed to delete_tcp_peer_resource()!\n");
		}

		return true;
	} else if(size < 0)
	{
		LOG_ERROR("Receive data from this peer failed, fd = %d!\n", sockfd);
		return false;
	}

	header = (struct itcgw_header *)rxbuff;
	header->msgno 			= ntohl(header->msgno);
	header->payloadLen 		= ntohl(header->payloadLen);
	header->protRev			= ntohl(header->protRev);
	header->receiver		= ntohl(header->receiver);
	header->sender			= ntohl(header->sender);

	LOG_INFO("Receiving %d bytes from fd %d\n", size, sockfd);
	LOG_INFO("Re-interpret TCP packet: msgno: 0x%08x\n", header->msgno);
	LOG_INFO("Re-interpret TCP packet: payloadLen: %u\n", header->payloadLen);
	LOG_INFO("Re-interpret TCP packet: protRev: %u\n", header->protRev);
	LOG_INFO("Re-interpret TCP packet: receiver: %u\n", header->receiver);
	LOG_INFO("Re-interpret TCP packet: sender: %u\n", header->sender);

	switch (header->msgno)
	{
	case ITCGW_ITC_DATA_FWD:
		LOG_INFO("Received ITCGW_ITC_DATA_FWD!\n");
		handle_receive_data_fwd(sockfd, header);
		break;
	
	case ITCGW_LOCATE_MBOX_REQUEST:
		LOG_INFO("Received ITCGW_LOCATE_MBOX_REQUEST!\n");
		handle_receive_locate_mbox(sockfd, header);
		break;
	
	default:
		LOG_ABN("Received unknown TCP packet!\n");
		break;
	}

	return true;
}

static bool delete_tcp_peer_resource(int sockfd)
{
	int i = 0;
	for(; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		if(itcgw_inst.tcp_server_peers[i].fd == sockfd)
		{
			close(itcgw_inst.tcp_server_peers[i].fd);
			itcgw_inst.tcp_server_peers[i].fd = -1;

			struct tcp_peer_info **iter;
			iter = tfind(itcgw_inst.tcp_server_peers[i].addr, &itcgw_inst.tcp_server_tree, compare_addr_tcp_tree);
			if(iter == NULL)
			{
				LOG_ABN("Disconnected peer not found in server tree, something wrong!\n");
				return false;
			}

			tdelete((*iter)->addr, &itcgw_inst.tcp_server_tree, compare_addr_tcp_tree);

			/* Notify UDP thread about our disconnected peer as well */
			union itc_msg *req;
			req = itc_alloc(offsetof(struct itcgw_udp_rmv_peer, addr) + strlen(itcgw_inst.tcp_server_peers[i].addr) + 1, ITCGW_UDP_RMV_PEER);
			strcpy(req->itcgw_udp_rmv_peer.addr, itcgw_inst.tcp_server_peers[i].addr);

			if(itc_send(&req, itcgw_inst.udp_mbox_id, ITC_MY_MBOX_ID, NULL) == false)
			{
				LOG_INFO("Failed to send ITCGW_UDP_RMV_PEER to mailbox %s!\n", ITC_GATEWAY_MBOX_UDP_NAME);
				itc_free(&req);
				return false;
			}

			strcpy(itcgw_inst.tcp_server_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING);
			return true;
		}
	}

	if(i == ITC_GATEWAY_MAX_PEERS)
	{
		LOG_ABN("Disconnected peer not found in server list, something wrong!\n");
		return false;
	}

	return true;
}

static void tcp_client_thread_destructor(void* data)
{
	(void)data;

	LOG_INFO("Calling tcp client thread destructor...\n");

	if(itcgw_inst.tcp_client_mbox_id != 0 || itcgw_inst.tcp_client_mbox_id != ITC_NO_MBOX_ID)
	{
		LOG_INFO("Deleting tcp client mailbox...\n");
		itc_delete_mailbox(itcgw_inst.tcp_client_mbox_id);
	}
}

static bool start_tcp_client_thread(void)
{
	MUTEX_LOCK(&itcgw_inst.tcp_client_mtx);
	int res = pthread_create(&itcgw_inst.tcp_client_tid, NULL, tcp_client_loop, NULL);
	if(res != 0)
	{
		LOG_ERROR("Failed to pthread_create, error code = %d\n", res);
		return false;
	}
	MUTEX_LOCK(&itcgw_inst.tcp_client_mtx); // Wait until tcp_client_thread finishes their initialization
	MUTEX_UNLOCK(&itcgw_inst.tcp_client_mtx);

	return true;
}

static void* tcp_client_loop(void *data)
{
	(void)data;

	if(prctl(PR_SET_NAME, "itc_gw_tcp_client", 0, 0, 0) == -1)
	{
		// ERROR trace is needed here
		LOG_ERROR("Failed to prctl() TCP client!\n");
		return NULL;
	}

	if(!setup_tcp_client_mailbox() || !setup_tcp_client_peer())
	{
		LOG_ERROR("Failed to setup_tcp_client_mailbox!\n");
		return NULL;
	}

	LOG_INFO("Starting tcp client loop...\n");

	int res = pthread_setspecific(itcgw_inst.tcp_client_destruct_key, (void*)(unsigned long)itcgw_inst.tcp_client_mbox_id);
	if(res != 0)
	{
		// ERROR trace is needed here
		LOG_ERROR("Failed to pthread_setspecific, error code = %d\n", res);
		return NULL;
	}

	MUTEX_UNLOCK(&itcgw_inst.tcp_client_mtx); // Done tcp_client_thread initialization, wake udp_thread up!

	fd_set fdset;
	int max_fd = -1;
	while(1)
	{
		FD_ZERO(&fdset);
		/* If some internal mailboxes want to send itc message outside our host,
		or udp mailbox wants to inform us some new peer that has sent us broadcasting message and just added into known host list,
		it will trigger this fd -> handle_receive_itcmsg_at_client() */
		FD_SET(itcgw_inst.tcp_client_mbox_fd, &fdset);
		max_fd = MAX_OF(itcgw_inst.tcp_client_mbox_fd, max_fd);

		// There is data coming from other peers on connect()-ed fd (cfm messages after sending out req messages)
		for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(itcgw_inst.tcp_client_peers[i].fd != -1)
			{
				FD_SET(itcgw_inst.tcp_client_peers[i].fd, &fdset);
				max_fd = MAX_OF(itcgw_inst.tcp_client_peers[i].fd, max_fd);
			}
		}

		res = select(max_fd + 1, &fdset, NULL, NULL, NULL);
		if(res < 0)
		{
			LOG_ERROR("Failed to select()!\n");
			return NULL;
		}

		if(FD_ISSET(itcgw_inst.tcp_client_mbox_fd, &fdset))
		{
			if(handle_receive_itcmsg_at_client(itcgw_inst.tcp_client_mbox_fd) == false)
			{
				LOG_ERROR("Failed to handle_receive_itcmsg_at_client()!\n");
				return NULL;
			}
		}

		for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
		{
			if(itcgw_inst.tcp_client_peers[i].fd != -1 && FD_ISSET(itcgw_inst.tcp_client_peers[i].fd, &fdset))
			{
				/* Note that: if we receiving nothing though this fd has been triggered, that means our client has been disconnected,
				remove peer from list and notify UDP mailbox so that they can remove peer from udp_list as well */
				if(handle_receive_tcp_packet_at_client(itcgw_inst.tcp_client_peers[i].fd) == false)
				{
					LOG_ERROR("Failed to handle_receive_tcp_packet_at_client()!\n");
					return NULL;
				}
			}
		}
	}

	return NULL;
}

static bool setup_tcp_client_mailbox(void)
{
	itcgw_inst.tcp_client_mbox_id = itc_create_mailbox(ITC_GATEWAY_MBOX_TCP_CLI_NAME, ITC_NO_NAMESPACE);
	if(itcgw_inst.tcp_client_mbox_id == ITC_NO_MBOX_ID)
	{
		LOG_ERROR("Failed to create mailbox %s\n", ITC_GATEWAY_MBOX_TCP_CLI_NAME);
		return false;
	}

	itcgw_inst.tcp_client_mbox_fd = itc_get_fd(itcgw_inst.tcp_client_mbox_id);
	LOG_INFO("Create TCP client mailbox \"%s\" successfully!\n", ITC_GATEWAY_MBOX_TCP_CLI_NAME);
	return true;
}

static bool setup_tcp_client_peer(void)
{
	for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		itcgw_inst.tcp_client_peers[i].fd = -1;
		strcpy(itcgw_inst.tcp_client_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING);
	}

	return true;
}

static bool handle_receive_itcmsg_at_client(int sockfd)
{
	(void)sockfd;
	union itc_msg *msg;

	msg = itc_receive(ITC_NO_WAIT);

	if(msg == NULL)
	{
		LOG_ERROR("Fatal error, itcgw received a NULL itc_msg!\n");
		return false;
	}

	switch (msg->msgno)
	{
	case ITCGW_UDP_ADD_PEER:
		LOG_INFO("Received ITCGW_UDP_ADD_PEER addr = %s\n", msg->itcgw_udp_add_peer.addr);
		LOG_INFO("Received ITCGW_UDP_ADD_PEER namespace = %s\n", msg->itcgw_udp_add_peer.namespace);
		handle_tcp_client_add_peer(msg->itcgw_udp_add_peer.addr, msg->itcgw_udp_add_peer.namespace);
		break;

	case ITC_FWD_DATA_TO_ITCGWS:
		LOG_INFO("Received ITC_FWD_DATA_TO_ITCGWS to namespace \"%s\"\n", msg->itc_fwd_data_to_itcgws.to_namespace);
		handle_fwd_data_out(msg);
		break;
	
	case ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST:
		LOG_INFO("Received ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST from itccoord mbox_id 0x%08x\n", msg->itc_locate_mbox_from_itcgws_request.itccoord_mboxid);
		LOG_INFO("Received ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST asked to locate mbox_name \"%s\"\n", msg->itc_locate_mbox_from_itcgws_request.mboxname);
		itcgw_inst.itccoord_mbox_id = msg->itc_locate_mbox_from_itcgws_request.itccoord_mboxid;
		handle_locate_mbox_request(msg);
		break;

	default:
		LOG_ABN("Received invalid message msgno = 0x%08x\n", msg->msgno);
		break;
	}

	itc_free(&msg);
	return true;
}

static bool handle_receive_tcp_packet_at_client(int sockfd)
{
	struct itcgw_header *header;
	int header_size = sizeof(struct itcgw_header);
	char rxbuff[header_size];
	int size = 0;

	size = recv_data(sockfd, rxbuff, header_size);

	if(size == 0)
	{
		LOG_INFO("Peer from this socket fd %d disconnected, remove it from client list!\n", sockfd);
		handle_tcp_client_rmv_peer(sockfd);
		return true;
		
	} else if(size < 0)
	{
		LOG_ERROR("Receive data from this peer failed, fd = %d!\n", sockfd);
		return false;
	}

	header = (struct itcgw_header *)rxbuff;
	header->msgno 			= ntohl(header->msgno);
	header->payloadLen 		= ntohl(header->payloadLen);
	header->protRev			= ntohl(header->protRev);
	header->receiver		= ntohl(header->receiver);
	header->sender			= ntohl(header->sender);

	LOG_INFO("Receiving %d bytes from fd %d\n", size, sockfd);
	LOG_INFO("Re-interpret TCP packet: msgno: 0x%08x\n", header->msgno);
	LOG_INFO("Re-interpret TCP packet: payloadLen: %u\n", header->payloadLen);
	LOG_INFO("Re-interpret TCP packet: protRev: %u\n", header->protRev);
	LOG_INFO("Re-interpret TCP packet: receiver: %u\n", header->receiver);
	LOG_INFO("Re-interpret TCP packet: sender: %u\n", header->sender);

	switch (header->msgno)
	{
	case ITCGW_LOCATE_MBOX_REPLY:
		LOG_INFO("Received ITCGW_LOCATE_MBOX_REPLY!\n");
		handle_receive_locate_mbox_reply(sockfd, header);
		break;
	
	default:
		LOG_ABN("Received unknown TCP packet!\n");
		break;
	}

	return true;
}

static bool handle_tcp_client_add_peer(char *addr, char *namespace)
{
	struct tcp_peer_info **iter;

	iter = tfind(addr, &itcgw_inst.tcp_client_tree, compare_addr_tcp_tree);
	if(iter != NULL)
	{
		LOG_ERROR("This peer \"%s\" already added in client tree, something wrong!\n", addr);
		return false;
	}

	char tcp_ip[25];
	uint16_t tcp_port;
	/* Instead of format string "%s" as usual, we must use "%[^:]" meaning read to string tcp_ip until character ':'. */
	int res = sscanf(addr, "tcp://%[^:]:%hu/", tcp_ip, &tcp_port);
	
	int new_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(new_fd < 0)
	{
		LOG_ERROR("Failed to get socket(), errno = %d!\n", errno);
		return false;
	}

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(tcp_ip);
	serveraddr.sin_port = htons(tcp_port);

	res = connect(new_fd, (struct sockaddr *)((void *)&serveraddr), sizeof(struct sockaddr_in));
	if(res < 0)
	{
		LOG_ERROR("Failed to get connect(), errno = %d!\n", errno);
		close(new_fd);
		return false;
	}

	int i = 0;
	for(; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		if(itcgw_inst.tcp_client_peers[i].fd == -1)
		{
			/* Allocate a slot for this new connection */
			LOG_INFO("Connect to new TCP peer successfully from hostname <%s> on tcp://%s:%hu/\n", namespace, tcp_ip, tcp_port);
			strcpy(itcgw_inst.tcp_client_peers[i].addr, addr);
			strcpy(itcgw_inst.tcp_client_peers[i].namespace, namespace);
			itcgw_inst.tcp_client_peers[i].fd = new_fd;
			tsearch(&itcgw_inst.tcp_client_peers[i], &itcgw_inst.tcp_client_tree, compare_peer_tcp_tree);
			break;
		}
	}

	if(i == ITC_GATEWAY_MAX_PEERS)
	{
		LOG_ERROR("No more than %d peers is accepted!\n", ITC_GATEWAY_MAX_PEERS);
		return false;
	}

	return true;
}

static bool handle_tcp_client_rmv_peer(int sockfd)
{
	int i = 0;
	for(; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		if(itcgw_inst.tcp_client_peers[i].fd == sockfd)
		{
			close(itcgw_inst.tcp_client_peers[i].fd);
			itcgw_inst.tcp_client_peers[i].fd = -1;

			struct tcp_peer_info **iter;
			iter = tfind(itcgw_inst.tcp_client_peers[i].addr, &itcgw_inst.tcp_client_tree, compare_addr_tcp_tree);
			if(iter == NULL)
			{
				LOG_ABN("Disconnected peer not found in client tree, something wrong!\n");
				return false;
			}

			tdelete((*iter)->addr, &itcgw_inst.tcp_client_tree, compare_addr_tcp_tree);

			strcpy(itcgw_inst.tcp_client_peers[i].addr, ITC_GATEWAY_NO_ADDR_STRING);
			return true;
		}
	}

	if(i == ITC_GATEWAY_MAX_PEERS)
	{
		LOG_ABN("Disconnected peer not found in client list, something wrong!\n");
		return false;
	}

	return true;
}

static bool handle_fwd_data_out(union itc_msg *msg)
{
	size_t msg_len = offsetof(struct itcgw_msg, payload) + offsetof(struct itcgw_itc_data_fwd, payload) + msg->itc_fwd_data_to_itcgws.payload_length;
	struct itcgw_msg *rep = malloc(msg_len);
	if(rep == NULL)
	{
		LOG_ERROR("Failed to malloc get namespace request message!\n");
		return false;
	}

	uint32_t payload_length = offsetof(struct itcgw_itc_data_fwd, payload) + msg->itc_fwd_data_to_itcgws.payload_length;
	rep->header.sender 					= htonl((uint32_t)getpid());
	rep->header.receiver 					= htonl(222);
	rep->header.protRev 					= htonl(15);
	rep->header.msgno 					= htonl(ITCGW_ITC_DATA_FWD);
	rep->header.payloadLen 					= htonl(payload_length);

	rep->payload.itcgw_itc_data_fwd.errorcode	= htonl(ITCGW_STATUS_OK);
	rep->payload.itcgw_itc_data_fwd.payload_length 	= htonl(msg->itc_fwd_data_to_itcgws.payload_length);
	memcpy(rep->payload.itcgw_itc_data_fwd.payload, msg->itc_fwd_data_to_itcgws.payload, msg->itc_fwd_data_to_itcgws.payload_length);

	// TODO: Find respective namespace -> corresponding sockfd
	struct tcp_peer_info **iter;
	iter = tfind(msg->itc_fwd_data_to_itcgws.to_namespace, &itcgw_inst.tcp_client_tree, compare_namespace_tcp_tree);
	if(iter == NULL)
	{
		LOG_ABN("Namespace \"%s\" is not available in client tree, maybe the respective peer is not connected to us yet!\n", msg->itc_fwd_data_to_itcgws.to_namespace);
		return true;
	}

	int res = send((*iter)->fd, rep, msg_len, 0);
	if(res < 0)
	{
		LOG_ERROR("Failed to send ITCGW_GET_NAMESPACE_REPLY, errno = %d!\n", errno);
		return false;
	}

	free(rep);
	LOG_INFO("Sent ITCGW_ITC_DATA_FWD to peer with namespace \"%s\" successfully!\n", (*iter)->namespace);
	return true;
}

static bool handle_receive_itcmsg_at_udp(int sockfd)
{
	(void)sockfd;
	union itc_msg *msg;

	msg = itc_receive(ITC_NO_WAIT);

	if(msg == NULL)
	{
		LOG_ERROR("Fatal error, itcgw received a NULL itc_msg!\n");
		return false;
	}

	switch (msg->msgno)
	{
	case ITCGW_UDP_RMV_PEER:
		LOG_INFO("Received ITCGW_UDP_RMV_PEER addr = %s\n", msg->itcgw_udp_rmv_peer.addr);
		handle_udp_rmv_peer(msg->itcgw_udp_rmv_peer.addr);
		break;

	case ITC_GET_NAMESPACE_REQUEST:
		LOG_INFO("Received ITC_GET_NAMESPACE_REQUEST from mbox 0x%08x\n", msg->itc_get_namespace_request.mbox_id);
		handle_udp_get_namespace_request(msg->itc_get_namespace_request.mbox_id);
		break;

	default:
		LOG_ABN("Received invalid message msgno = 0x%08x\n", msg->msgno);
		break;
	}

	itc_free(&msg);
	return true;
}

static bool handle_udp_rmv_peer(char *addr)
{
	struct udp_peer_info **iter;

	iter = tfind(addr, &itcgw_inst.udp_tree, compare_addr_udp_tree);
	if(iter == NULL)
	{
		LOG_ERROR("This peer \"%s\" not found in udp tree, something wrong!\n", addr);
		return false;
	}

	strcpy((*iter)->addr, ITC_GATEWAY_NO_ADDR_STRING);
	tdelete(*iter, &itcgw_inst.udp_tree, compare_peer_udp_tree);

	LOG_INFO("Remove peer \"%s\" from udp tree successfully!\n", addr);
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
	} while(nr_bytes_to_read > 0);

	return read_count;
}

static bool handle_udp_get_namespace_request(itc_mbox_id_t mbox_id)
{
	union itc_msg *rep;
	rep = itc_alloc(offsetof(struct itc_get_namespace_reply, namespace) + strlen(itcgw_inst.namespace) + 1, ITC_GET_NAMESPACE_REPLY);
	strcpy(rep->itc_get_namespace_reply.namespace, itcgw_inst.namespace);

	if(itc_send(&rep, mbox_id, ITC_MY_MBOX_ID, NULL) == false)
	{
		LOG_ERROR("Failed to send ITC_GET_NAMESPACE_REPLY to mailbox 0x%08x\n", mbox_id);
		itc_free(&rep);
		return false;
	}

	LOG_INFO("Sent ITC_GET_NAMESPACE_REPLY to mailbox 0x%08x successfully!\n", mbox_id);
	return true;
}

static bool handle_receive_data_fwd(int sockfd, struct itcgw_header *header)
{
	struct itcgw_itc_data_fwd *rep;
	uint32_t payloadLen = header->payloadLen;
	char rxbuff[payloadLen];
	int size = 0;

	size = recv_data(sockfd, rxbuff, payloadLen);

	if(size <= 0)
	{
		LOG_ERROR("Failed to receive data from this peer, fd = %d!\n", sockfd);
		return false;
	}

	rep = (struct itcgw_itc_data_fwd *)rxbuff;
	rep->errorcode			= ntohl(rep->errorcode);
	rep->payload_length 		= ntohl(rep->payload_length);

	LOG_INFO("Receiving %d bytes from fd %d\n", size, sockfd);
	LOG_INFO("Re-interpret TCP packet: errorcode: %u\n", rep->errorcode);
	LOG_INFO("Re-interpret TCP packet: payload_length: \"%u\"\n", rep->payload_length);
	
	union itc_msg *msg;
	msg = itc_alloc(((struct itc_message *)&rep->payload)->size, ((struct itc_message *)&rep->payload)->msgno);
	struct itc_message *message = CONVERT_TO_MESSAGE(msg);

	memcpy(message, ((struct itc_message *)&rep->payload), rep->payload_length);

	LOG_INFO("Received not-known-yet message msgno 0x%08x, from a mbox 0x%08x outside our host!\n", message->msgno, message->sender);

	if(!itc_send(&msg, message->receiver, ITC_MY_MBOX_ID, NULL))
	{
		LOG_ERROR("Failed to send the message to our internal mailbox 0x%08x\n", message->receiver);
		return false;
	}
	
	LOG_INFO("Forwardeding message to our internal mailbox 0x%08x\n", message->receiver);
	return true;
}

static bool handle_locate_mbox_request(union itc_msg *msg)
{
	size_t msg_len = offsetof(struct itcgw_msg, payload) + offsetof(struct itcgw_locate_mbox_request, mboxname) + strlen(msg->itc_locate_mbox_from_itcgws_request.mboxname) + 1;
	struct itcgw_msg *rep = malloc(msg_len);
	if(rep == NULL)
	{
		LOG_ERROR("Failed to malloc get namespace request message!\n");
		return false;
	}

	uint32_t payload_length = offsetof(struct itcgw_locate_mbox_request, mboxname) + strlen(msg->itc_locate_mbox_from_itcgws_request.mboxname) + 1;
	rep->header.sender 					= htonl((uint32_t)getpid());
	rep->header.receiver 					= htonl(111);
	rep->header.protRev 					= htonl(15);
	rep->header.msgno 					= htonl(ITCGW_LOCATE_MBOX_REQUEST);
	rep->header.payloadLen 					= htonl(payload_length);

	rep->payload.itcgw_locate_mbox_request.errorcode	= htonl(ITCGW_STATUS_OK);
	strcpy(rep->payload.itcgw_locate_mbox_request.mboxname, msg->itc_locate_mbox_from_itcgws_request.mboxname);

	/* Send this locate mbox request to all connected TCP peers asking them to see if they have this mboxname */
	int count = 0;
	for(int i = 0; i < ITC_GATEWAY_MAX_PEERS; i++)
	{
		if(itcgw_inst.tcp_client_peers[i].fd != -1)
		{
			int res = send(itcgw_inst.tcp_client_peers[i].fd, rep, msg_len, 0);
			if(res < 0)
			{
				LOG_ERROR("Failed to send ITCGW_LOCATE_MBOX_REQUEST, errno = %d!\n", errno);
				continue;
			} else
			{
				count++;
			}
		}
	}

	if(count == 0)
	{
		free(rep);
		LOG_ABN("Could not send ITCGW_LOCATE_MBOX_REQUEST since client list have no connected hosts yet!\n", count);
		union itc_msg *msg;
		msg = itc_alloc(offsetof(struct itc_locate_mbox_from_itcgws_reply, namespace) + 1, ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY);

		msg->itc_locate_mbox_from_itcgws_reply.mbox_id = ITC_NO_MBOX_ID;
		strcpy(msg->itc_locate_mbox_from_itcgws_reply.namespace, "");

		if(itc_send(&msg, itcgw_inst.itccoord_mbox_id, ITC_MY_MBOX_ID, NULL) == false)
		{
			LOG_ERROR("Failed to send ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY to itccoord!\n");
			itc_free(&msg);
			return false;
		}
		
		LOG_INFO("Sent back ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY to itccoord without any results!\n");
		return true;
	}

	free(rep);
	LOG_INFO("Broadcast ITCGW_LOCATE_MBOX_REQUEST to all %d peers successfully!\n", count);
	return true;
}

static bool handle_receive_locate_mbox(int sockfd, struct itcgw_header *header)
{
	struct itcgw_locate_mbox_request *rep;
	uint32_t payloadLen = header->payloadLen;
	char rxbuff[payloadLen];
	int size = 0;

	size = recv_data(sockfd, rxbuff, payloadLen);

	if(size <= 0)
	{
		LOG_ERROR("Failed to receive data from this peer, fd = %d!\n", sockfd);
		return false;
	}

	rep = (struct itcgw_locate_mbox_request *)rxbuff;
	rep->errorcode			= ntohl(rep->errorcode);

	LOG_INFO("Receiving %d bytes from fd %d\n", size, sockfd);
	LOG_INFO("Re-interpret TCP packet: errorcode: %u\n", rep->errorcode);
	LOG_INFO("Re-interpret TCP packet: mboxname: \"%s\"\n", rep->mboxname);

	int32_t timeout = 1000; // Wait max 1000 ms for locating mailbox name
	itc_mbox_id_t mbox_id = itc_locate_sync(timeout, rep->mboxname, 1, NULL, NULL);
	if(mbox_id == ITC_NO_MBOX_ID)
	{
		LOG_ERROR("Failed to locate mailbox %s even after %d ms!\n", rep->mboxname, timeout);
		return false;
	}

	if(!send_locate_mbox_reply(sockfd, mbox_id))
	{
		LOG_ERROR("Failed to send_locate_mbox_reply()!\n");
		return false;
	}

	return true;
}

static bool send_locate_mbox_reply(int sockfd, itc_mbox_id_t mbox_id)
{
	size_t msg_len = offsetof(struct itcgw_msg, payload) + sizeof(struct itcgw_locate_mbox_reply);
	struct itcgw_msg *rep = malloc(msg_len);
	if(rep == NULL)
	{
		LOG_ERROR("Failed to malloc locate mbox request message!\n");
		return false;
	}

	uint32_t payload_length = sizeof(struct itcgw_locate_mbox_reply);
	rep->header.sender 					= htonl((uint32_t)getpid());
	rep->header.receiver 					= htonl(111);
	rep->header.protRev 					= htonl(15);
	rep->header.msgno 					= htonl(ITCGW_LOCATE_MBOX_REPLY);
	rep->header.payloadLen 					= htonl(payload_length);

	rep->payload.itcgw_locate_mbox_reply.errorcode	= htonl(ITCGW_STATUS_OK);
	rep->payload.itcgw_locate_mbox_reply.mbox_id	= htonl(mbox_id);

	int res = send(sockfd, rep, msg_len, 0);
	if(res < 0)
	{
		LOG_ERROR("Failed to send ITCGW_LOCATE_MBOX_REPLY, errno = %d!\n", errno);
		return false;
	}

	free(rep);
	LOG_INFO("Sent ITCGW_LOCATE_MBOX_REPLY successfully!\n");
	return true;
}

static bool handle_receive_locate_mbox_reply(int sockfd, struct itcgw_header *header)
{
	struct itcgw_locate_mbox_reply *rep;
	uint32_t payloadLen = header->payloadLen;
	char rxbuff[payloadLen];
	int size = 0;

	size = recv_data(sockfd, rxbuff, payloadLen);

	if(size <= 0)
	{
		LOG_ERROR("Failed to receive data from this peer, fd = %d!\n", sockfd);
		return false;
	}

	rep = (struct itcgw_locate_mbox_reply *)rxbuff;
	rep->errorcode 	= ntohl(rep->errorcode);
	rep->mbox_id 	= ntohl(rep->mbox_id);

	LOG_INFO("Receiving %d bytes from fd %d\n", size, sockfd);
	LOG_INFO("Re-interpret TCP packet: errorcode: %u\n", rep->errorcode);
	LOG_INFO("Re-interpret TCP packet: mbox_id: 0x%08x\n", rep->mbox_id);

	struct tcp_peer_info **iter;
	iter = tfind(&sockfd, &itcgw_inst.tcp_client_tree, compare_sockfd_tcp_tree);
	if(iter == NULL)
	{
		LOG_ERROR("Peer with fd = %d not found in client tree, something wrong!\n", sockfd);
		return false;
	}

	union itc_msg *msg;
	msg = itc_alloc(offsetof(struct itc_locate_mbox_from_itcgws_reply, namespace) + strlen((*iter)->namespace) + 1, ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY);

	msg->itc_locate_mbox_from_itcgws_reply.mbox_id = rep->mbox_id;
	strcpy(msg->itc_locate_mbox_from_itcgws_reply.namespace, (*iter)->namespace);

	if(itc_send(&msg, itcgw_inst.itccoord_mbox_id, ITC_MY_MBOX_ID, NULL) == false)
	{
		LOG_ERROR("Failed to send ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY to itccoord!\n");
		itc_free(&msg);
		return false;
	}
	
	LOG_INFO("Sent ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY to itccoord successfully!\n");
	return true;
}



















