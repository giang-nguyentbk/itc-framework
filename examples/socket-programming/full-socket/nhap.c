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
#define TCP_LISTENING_PORT	11111
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


static bool setup_my_TCP(void);
static struct in_addr get_ip_address_from_network_interface(int sockfd, char *interface);
static int recv_data(int sockfd, void *rx_buff, int nr_bytes_to_read);



int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	setup_my_TCP();

	int res;
	fd_set fdset;
	int max_fd;
	while(!is_terminated)
	{
		FD_ZERO(&fdset);
		FD_SET(m_inst.my_TCP_fd, &fdset);
		max_fd = m_inst.my_TCP_fd + 1;

		res = select(max_fd, &fdset, NULL, NULL, NULL);
		if(res < 0)
		{
			printf("\tERROR: \"%s:%d\" main - Failed to select()!\n", __FILE__, __LINE__);
			exit(EXIT_FAILURE);
		}

		if(FD_ISSET(m_inst.my_TCP_fd, &fdset))
		{
			char rxbuff[1500];
			int to_read = 1500;
			
			recv_data(m_inst.my_TCP_fd, rxbuff, to_read);
			printf("\tINFO: \"%s:%d\" main - Receiving connect() from some peer rxbuff = \"%d\"!\n", __FILE__, __LINE__, rxbuff[0]);
			printf("\tINFO: \"%s:%d\" main - Receiving connect() from some peer rxbuff = \"%d\"!\n", __FILE__, __LINE__, rxbuff[1]);
			printf("\tINFO: \"%s:%d\" main - Receiving connect() from some peer rxbuff = \"%d\"!\n", __FILE__, __LINE__, rxbuff[2]);
		}


	}

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
	printf("\tINFO: \"%s:%d\" setup_my_TCP - Setup my TCP faillll on %s:%d\n", __FILE__, __LINE__, inet_ntoa(get_ip_address_from_network_interface(tcpfd, NETWORK_INTERFACE_ETH0)), htons(TCP_LISTENING_PORT));

	res = bind(tcpfd, (struct sockaddr *)&m_inst.my_TCP_addr, size);
	if(res < 0)
	{
		printf("\tERROR: \"%s:%d\" setup_my_TCP - Failed to bind, errno = %d!\n", __FILE__, __LINE__, errno);
		close(tcpfd);
		return false;
	}

	res = listen(tcpfd, 10);
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