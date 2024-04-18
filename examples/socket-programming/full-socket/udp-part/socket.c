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

/* This is a full socket program. Alright, you may wonder what is meant by "fully" socket program.
	+ A full socket program will include:
		1. An UDP Client: responsible for broadcasting greeting messages, something like "Are you a "fully socket program" like me?
		If yes and want to establish connection with me, just connect to my TCP endpoint here, tcp://192.168.1.5:22222."
		
		2. An UDP Server: responsible for receiving and handling greeting messages above. If it comes from an already connected peer, ignore it.
		Otherwise, send a get_namespace request to the endpoint tcp://192.168.1.5:22222, for example.

		3. An TCP Server: responsible for create socket fd, bind it to internet addresses and listening on it.  



*/

#define BROADCAST_PORT		11111
#define BROADCAST_PORT2		11112
#define BROADCAST_INTERVAL	50000
#define ETHERNET_PACKET_SIZE	1500
#define NETWORK_INTERFACE_ETH0	"eth0"
#define NETWORK_INTERFACE_LO	"lo"


// struct my_peer_info {
// 	int			peer_UDP_fd
// }

struct my_instance {
	/* my UDP part */
	int			my_UDP_fd;
	struct sockaddr_in	my_UDP_addr;

	/* peer UDP part */
	struct sockaddr_in	peer_UDP_addr;
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
static struct in_addr get_ip_address_from_network_interface(int sockfd, char *interface);
static bool handle_receive_greeting_msg(int sockfd);





/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	signal(SIGINT, interrupt_handler);

	setup_my_UDP();

	get_ip_address_from_network_interface(m_inst.my_UDP_fd, NETWORK_INTERFACE_ETH0);
	get_ip_address_from_network_interface(m_inst.my_UDP_fd, NETWORK_INTERFACE_LO);
	
	// char *greeting_msg = malloc(ITCGW_ETHERNET_PACKET_SIZE);
	// if(greeting_msg == NULL)
	// {
	// 	printf("\tDEBUG: itcgw - Failed to malloc greeting message!\n");
	// 	exit(EXIT_FAILURE);
	// }

	// int greeting_msg_length = snprintf((char *)greeting_msg, ITCGW_ETHERNET_PACKET_SIZE, "ITC Gateway Broadcast Greeting Message from tcp://%s:%d/",
	// 	inet_ntoa(server_addr), ITCGW_LISTENING_PORT);

	char *greeting_msg = "Broadcast Greeting Message!";
	
	setup_peer_UDP();

	int res;
	fd_set fdset;
	int max_fd;
	// uint32_t counter = BROADCAST_INTERVAL;
	while(!is_terminated)
	{
		/* 1. Broadcasting greeting messages: */
		// if(counter >= BROADCAST_INTERVAL)
		// {
			res = sendto(m_inst.my_UDP_fd, greeting_msg, strlen(greeting_msg), 0, (struct sockaddr *)((void *)&m_inst.peer_UDP_addr), sizeof(struct sockaddr_in));
			if(res < 0)
			{
				printf("\tERROR: \"%s:%d\" main - Failed to broadcast greeting message, errno = %d!\n", __FILE__, __LINE__, errno);
				exit(EXIT_FAILURE);
			}

			/* Reset counter */
		// 	counter = 0;
		// }

		FD_ZERO(&fdset);
		FD_SET(m_inst.my_UDP_fd, &fdset);
		max_fd = m_inst.my_UDP_fd + 1;

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

		// counter++;
		sleep(5);
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
			printf("\tERROR: \"%s:%d\" main - Failed to receive, errno = %d!\n", __FILE__, __LINE__, errno);
		} else
		{
			printf("\tERROR: \"%s:%d\" main - Receiving message was interrupted, continue receiving!\n", __FILE__, __LINE__);
		}
		return false;
	}

	printf("\tINFO: \"%s:%d\" main - Receiving msg = %s!\n", __FILE__, __LINE__, rx_buff);
	return true;
}