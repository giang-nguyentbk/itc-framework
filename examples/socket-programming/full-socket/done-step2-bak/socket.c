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
#include <sys/timerfd.h>

#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* This is a full socket program. Alright, you may wonder what is meant by "fully" socket program.
	+ A full socket program will include:
		1. An UDP Client: responsible for broadcasting greeting messages, something like "Are you a "fully socket program" like me?
		If yes and want to establish connection with me, just connect to my TCP endpoint here, tcp://192.168.1.5:22222."
		
		2. An UDP Server: responsible for receiving and handling greeting messages above. If it comes from an already connected peer, ignore it.
		Otherwise, send a get_namespace request to the endpoint tcp://192.168.1.5:22222, for example.

		3. An TCP Server: responsible for create socket fd, bind it to internet addresses and listening on it.  

		4. An TCP Client: responsible for sending messages.


Connect sequence:
Step 1:		Peer A: Broadcasting greeting messages					: No peer in the A's list
Step 2:		Peer B: Received above message						: Add peer A to the B list with PEER_CONNECTING state
Step 3:		Peer B: connect() which triggers peer A accept()			: Add peer B to the A list with PEER_CONNECTING state
Step 4:		Peer B: Send get_namespace request along with B's namespace		: Peer A state in B list is still PEER_CONNECTING until peer B receiving get_namespace reply
Step 5:		Peer A: Received above request						: Set peer B state in A list to PEER_CONNECTED
Step 6:		Peer A: Send get_namespace reply					:
Step 7:		Peer B: Received above reply						: Set peer A state in B list to PEER_CONNECTED


*/

#define BROADCAST_PORT		11111
#define BROADCAST_PORT2		11112 // TEST ONLY
#define TCP_LISTENING_PORT	22222
#define TCP_LISTENING_PORT2	22223 // TEST ONLY
#define BROADCAST_INTERVAL	5
#define ETHERNET_PACKET_SIZE	1500
#define NETWORK_INTERFACE_ETH0	"eth0"
#define NETWORK_INTERFACE_LO	"lo"
#define MAX_PEERS		255
#define INVALID_FD		-1


typedef enum {
	PEER_DISCONNECTED = 0,
	PEER_CONNECTING, // After receiving greeting message from some peer, and before 
	PEER_CONNECTED, // After some peer connect() to our TCP listening port, and we accept() it.
	PEER_INVALID
} peer_state_e;

struct peer_info {
	char			namespace[255];
	struct sockaddr_in	tcp_addr;
	int			sockfd;
	peer_state_e		state;
};

struct my_instance {
	/* my UDP part */
	int			my_UDP_fd;
	struct sockaddr_in	my_UDP_addr;
	int			my_UDP_broadcast_timer_fd;

	/* peer UDP part */
	struct sockaddr_in	peer_UDP_addr;

	/* my TCP part */
	int			my_TCP_fd;
	struct sockaddr_in	my_TCP_addr;

	/* Temp */
	struct sockaddr_in	peer_who_sent_greeting_msg;

	/* Manage peer TCP info */
	struct peer_info	peers[MAX_PEERS];
	void			*peer_tree; // Used for quickly searching for a specific peer

	/* Greeting message */
	char			greeting_msg[512];
};


static struct my_instance m_inst;
static volatile bool is_terminated = false;

void interrupt_handler(int dummy) {
	(void)dummy;
	is_terminated = true;
}


/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static bool setup_my_UDP(void);
static void setup_peer_UDP(void);
static bool setup_my_TCP(void);
static void setup_peer_list(void);
static struct in_addr get_ip_address_from_network_interface(int sockfd, char *interface);
static bool handle_receive_greeting_msg(int sockfd);
static bool check_broadcast_timer(int *timer_fd, time_t interval);
static bool create_greeting_msg(void);
static int compare_peer_in_peer_tree(const void *pa, const void *pb);





/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	signal(SIGINT, interrupt_handler);

	setup_my_UDP();
	setup_peer_UDP();
	setup_my_TCP();
	setup_peer_list();

	get_ip_address_from_network_interface(m_inst.my_UDP_fd, NETWORK_INTERFACE_ETH0);
	get_ip_address_from_network_interface(m_inst.my_UDP_fd, NETWORK_INTERFACE_LO);
	
	create_greeting_msg();

	int res;
	fd_set fdset;
	int max_fd;
	while(!is_terminated)
	{
		check_broadcast_timer(&m_inst.my_UDP_broadcast_timer_fd, BROADCAST_INTERVAL);

		FD_ZERO(&fdset);
		FD_SET(m_inst.my_UDP_fd, &fdset);
		max_fd = m_inst.my_UDP_fd + 1;
		FD_SET(m_inst.my_UDP_broadcast_timer_fd, &fdset);
		max_fd = m_inst.my_UDP_broadcast_timer_fd > max_fd ? m_inst.my_UDP_broadcast_timer_fd + 1 : max_fd;

		res = select(max_fd, &fdset, NULL, NULL, NULL);
		if(res < 0)
		{
			printf("\tERROR: \"%s:%d\" main - Failed to select()!\n", __FILE__, __LINE__);
			exit(EXIT_FAILURE);
		}

		if(FD_ISSET(m_inst.my_UDP_fd, &fdset))
		{
			if(handle_receive_greeting_msg(m_inst.my_UDP_fd) == false)
			{
				printf("\tERROR: \"%s:%d\" main - Failed to handle_receive_greeting_msg()!\n", __FILE__, __LINE__);
				exit(EXIT_FAILURE);
			}
		}
	}


	close(m_inst.my_UDP_fd);

	exit(EXIT_SUCCESS);
}




/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static bool setup_my_UDP(void)
{
	m_inst.my_UDP_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(m_inst.my_UDP_fd < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_UDP - Failed to create socket(), errno = %d!\n", __FILE__, __LINE__, errno);
		return false;
	}

	int broadcast_opt = 1;
	int res = setsockopt(m_inst.my_UDP_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_UDP - Failed to setsockopt() SO_BROADCAST, errno = %d!\n", __FILE__, __LINE__, errno);
		close(m_inst.my_UDP_fd);
		return false;
	}

	res = setsockopt(m_inst.my_UDP_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_UDP - Failed to setsockopt() SO_REUSEADDR, errno = %d!\n", __FILE__, __LINE__, errno);
		close(m_inst.my_UDP_fd);
		return false;
	}

	struct sockaddr_in myUDPaddr;
	size_t size = sizeof(struct sockaddr_in);
	memset(&myUDPaddr, 0, size);
	myUDPaddr.sin_family = AF_INET;
	myUDPaddr.sin_addr.s_addr = INADDR_ANY;
	myUDPaddr.sin_port = htons(BROADCAST_PORT);

	res = bind(m_inst.my_UDP_fd, (struct sockaddr *)((void *)&myUDPaddr), size);
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_UDP - Failed to bind(), errno = %d!\n", __FILE__, __LINE__, errno);
		close(m_inst.my_UDP_fd);
		return false;
	}

	m_inst.my_UDP_addr = myUDPaddr;

	printf("\tINFO: \"%s:%d\" setup_my_UDP - Setup my UDP successfully on %s:%d\n", __FILE__, __LINE__, inet_ntoa(myUDPaddr.sin_addr), ntohs(myUDPaddr.sin_port));
	return true;
}

static void setup_peer_UDP(void)
{
	/* This is address configuration of peer broadcast UDP */
	memset(&m_inst.peer_UDP_addr, 0, sizeof(struct sockaddr_in));
	m_inst.peer_UDP_addr.sin_family = AF_INET;
	// m_inst.peer_UDP_addr.sin_port = htons((short)(BROADCAST_PORT & 0xFFFF));
	m_inst.peer_UDP_addr.sin_port = htons((short)(BROADCAST_PORT2 & 0xFFFF)); // TEST ONLY
	m_inst.peer_UDP_addr.sin_addr.s_addr = INADDR_BROADCAST;

	// printf("\tINFO: \"%s:%d\" setup_peer_UDP - Setup peer UDP successfully on %s:%d\n", __FILE__, __LINE__, inet_ntoa(m_inst.peer_UDP_addr.sin_addr), BROADCAST_PORT);
	printf("\tINFO: \"%s:%d\" setup_peer_UDP - Setup peer UDP successfully on %s:%d\n", __FILE__, __LINE__, inet_ntoa(m_inst.peer_UDP_addr.sin_addr), BROADCAST_PORT2); // TEST ONLY
}

static bool setup_my_TCP(void)
{
	int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
	if(tcpfd < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_TCP - Failed to get socket(), errno = %d!\n", __FILE__, __LINE__, errno);
		return false;
	}

	int listening_opt = 1;
	int res = setsockopt(tcpfd, SOL_SOCKET, SO_REUSEADDR, &listening_opt, sizeof(int));
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_TCP - Failed to set sockopt SO_REUSEADDR, errno = %d!\n", __FILE__, __LINE__, errno);
		close(tcpfd);
		return false;
	}

	memset(&m_inst.my_TCP_addr, 0, sizeof(struct sockaddr_in));
	size_t size = sizeof(struct sockaddr_in);
	m_inst.my_TCP_addr.sin_family = AF_INET;
	m_inst.my_TCP_addr.sin_addr = get_ip_address_from_network_interface(tcpfd, NETWORK_INTERFACE_ETH0);
	m_inst.my_TCP_addr.sin_port = htons(TCP_LISTENING_PORT);

	res = bind(tcpfd, (struct sockaddr *)&m_inst.my_TCP_addr, size);
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_TCP - Failed to bind, errno = %d!\n", __FILE__, __LINE__, errno);
		close(tcpfd);
		return false;
	}

	res = listen(tcpfd, MAX_PEERS);
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_TCP - Failed to listen, errno = %d!\n", __FILE__, __LINE__, errno);
		close(tcpfd);
		return false;
	}

	m_inst.my_TCP_fd = tcpfd;

	printf("\tINFO: \"%s:%d\" setup_my_TCP - Setup my TCP successfully on %s:%d\n", __FILE__, __LINE__, inet_ntoa(m_inst.my_TCP_addr.sin_addr), ntohs(m_inst.my_TCP_addr.sin_port));
	return true;
}

static void setup_peer_list(void)
{
	for(int i = 0; i < MAX_PEERS; i++)
	{
		m_inst.peers[i].sockfd = INVALID_FD;
		m_inst.peers[i].state = PEER_DISCONNECTED;
		strcpy(m_inst.peers[i].namespace, ""); 
	}
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
		printf("\tERROR: \"%s:%d\" get_ip_address_from_network_interface - Failed to ioctl to obtain IP address from %s, errno = %d!\n", __FILE__, __LINE__, interface, errno);
		return sock_addr.sin_addr;
	}

	size = sizeof(struct sockaddr_in);
	memcpy(&sock_addr, &(ifrq.ifr_ifru.ifru_addr), size);

	printf("\tINFO: \"%s:%d\" get_ip_address_from_network_interface - Address from network interface \"%s\" tcp://%s:%d\n", __FILE__, __LINE__, interface, inet_ntoa(sock_addr.sin_addr), sock_addr.sin_port);

	return sock_addr.sin_addr;
}

static bool handle_receive_greeting_msg(int sockfd)
{
	struct sockaddr_in m_peerUDPaddr;
	char rx_buff[ETHERNET_PACKET_SIZE];
	socklen_t length = sizeof(struct sockaddr_in);

	memset(&m_peerUDPaddr, 0, length);

	int res = recvfrom(sockfd, rx_buff, ETHERNET_PACKET_SIZE, 0, (struct sockaddr *)((void *)&m_peerUDPaddr), &length);
	if(res < 0)
	{
		if(errno != EINTR)
		{
			printf("\tERROR: \"%s:%d\" handle_receive_greeting_msg - Failed to receive, errno = %d!\n", __FILE__, __LINE__, errno);
		} else
		{
			printf("\tERROR: \"%s:%d\" handle_receive_greeting_msg - Receiving message was interrupted, continue receiving!\n", __FILE__, __LINE__);
		}
		return false;
	}

	rx_buff[res] = '\0';

	char tcp_ip[100];
	uint16_t tcp_port;
	/* Instead of format string "%s" as usual, we must use "%[^:]" meaning read to string tcp_ip until character ':'. */
	res = sscanf(rx_buff, "Broadcast Greeting Message from tcp://%[^:]:%hu/", tcp_ip, &tcp_port);

	printf("\tINFO: \"%s:%d\" handle_receive_greeting_msg - Received a greeting message from tcp://%s:%hu/\n", __FILE__, __LINE__, tcp_ip, tcp_port);


	struct peer_info **iter;
	struct peer_info peer;
	peer.tcp_addr.sin_addr.s_addr = inet_addr(tcp_ip);
	peer.tcp_addr.sin_port = tcp_port;
	iter = tfind(&peer, &m_inst.peer_tree, compare_peer_in_peer_tree);

	if(iter != NULL)
	{
		/* Already added in tree */
		printf("\tINFO: \"%s:%d\" handle_receive_greeting_msg - Already connected, ignore this greeting message!\n", __FILE__, __LINE__);
	} else
	{
		for(int i = 0; i < MAX_PEERS; i++)
		{
			if(m_inst.peers[i].sockfd == INVALID_FD && m_inst.peers[i].state == PEER_DISCONNECTED)
			{
				/* Allocate a slot for this new connection */
				/* sockfd and namespace will be populated after receiving get_namespace reply, and state will be changed to PEER_CONNECTED as well */
				printf("\tINFO: \"%s:%d\" handle_receive_greeting_msg - Adding new TCP peer connection successfully from tcp://%s:%hu/\n", __FILE__, __LINE__, tcp_ip, tcp_port);
				m_inst.peers[i].tcp_addr.sin_addr.s_addr = peer.tcp_addr.sin_addr.s_addr;
				m_inst.peers[i].tcp_addr.sin_port = peer.tcp_addr.sin_port;
				m_inst.peers[i].state = PEER_CONNECTING;
				tsearch(&m_inst.peers[i], &m_inst.peer_tree, compare_peer_in_peer_tree);
				break;
			}
		}
	}




	return true;
}

static bool check_broadcast_timer(int *timer_fd, time_t interval)
{
	struct timespec now;
	struct itimerspec remaining_time;
	struct itimerspec its;
	int res;

	if(*timer_fd == 0)
	{
		*timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
		if(*timer_fd < 0)
		{
			printf("\tERROR: \"%s:%d\" check_broadcast_timer - Failed to timerfd_create(), errno = %d!\n", __FILE__, __LINE__, errno);
			return false;
		}
	}

	res = timerfd_gettime(*timer_fd, &remaining_time);
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" check_broadcast_timer - Failed to timerfd_gettime(), errno = %d!\n", __FILE__, __LINE__, errno);
		return false;
	}

	printf("\tINFO: \"%s:%d\" check_broadcast_timer - Remaining timer: %ld,%ld seconds!\n", __FILE__, __LINE__, remaining_time.it_value.tv_sec, remaining_time.it_value.tv_nsec / 1000000);
	
	clock_gettime(CLOCK_REALTIME, &now);
	if(remaining_time.it_value.tv_sec == 0 && remaining_time.it_value.tv_nsec == 0)
	{
		printf("\tINFO: \"%s:%d\" check_broadcast_timer - Reset timer %lds, broadcasting greeting message!\n", __FILE__, __LINE__, interval);
		memset(&its, 0, sizeof(struct itimerspec));
		its.it_value.tv_sec = now.tv_sec + (time_t)interval;
		its.it_value.tv_nsec = now.tv_nsec;
		res = timerfd_settime(*timer_fd, TFD_TIMER_ABSTIME, &its, NULL);
		if(res < 0)
		{
			printf("\tERROR: \"%s:%d\" check_broadcast_timer - Failed to timerfd_settime(), errno = %d!\n", __FILE__, __LINE__, errno);
			return false;
		}

		/* 1. Broadcasting greeting messages: */
		res = sendto(m_inst.my_UDP_fd, m_inst.greeting_msg, strlen(m_inst.greeting_msg), 0, (struct sockaddr *)((void *)&m_inst.peer_UDP_addr), sizeof(struct sockaddr_in));
		if(res < 0)
		{
			printf("\tERROR: \"%s:%d\" check_broadcast_timer - Failed to broadcast greeting message, errno = %d!\n", __FILE__, __LINE__, errno);
			return false;
		}
	}

	return true;
}

static bool create_greeting_msg(void)
{
	snprintf((char *)m_inst.greeting_msg, 512, "Broadcast Greeting Message from tcp://%s:%hu/", inet_ntoa(m_inst.my_TCP_addr.sin_addr), ntohs(m_inst.my_TCP_addr.sin_port));

	printf("\tINFO: \"%s:%d\" create_greeting_msg - greeting_msg = \"%s\"\n", __FILE__, __LINE__, m_inst.greeting_msg);
	return true;
}

static int compare_peer_in_peer_tree(const void *pa, const void *pb)
{
	const struct peer_info *peer_a = pa;
	const struct peer_info *peer_b = pb;
	
	if(peer_a->tcp_addr.sin_addr.s_addr == peer_b->tcp_addr.sin_addr.s_addr && peer_a->tcp_addr.sin_port == peer_b->tcp_addr.sin_port)
	{
		return 0;
	} else if(peer_a->tcp_addr.sin_addr.s_addr > peer_b->tcp_addr.sin_addr.s_addr)
	{
		return 1;
	} else
	{
		return -1;
	}
}




