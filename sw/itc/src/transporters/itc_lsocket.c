#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <pthread.h>
#include <search.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"
#include "itc_proto.h"


/*****************************************************************************\/
*****                    INTERNAL TYPES IN LSOCK-ATOR                       *****
*******************************************************************************/
#define RXBUF_LEN 1024

struct lsock_instance {
	int			sd; // socket descriptor
	bool			is_coord_running; // to see if itccoord is running, which is received in locate_cfm
	bool			is_path_created;
};


/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LSOCK-ATOR                    *****
*******************************************************************************/
static struct lsock_instance lsock_inst;


/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void generate_lsockpath(struct result_code* rc);


/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static bool lsock_locate_coord(struct result_code* rc, itc_mbox_id_t* my_mbox_id_in_itccoord, \
			itc_mbox_id_t* itccoord_mask, itc_mbox_id_t* itccoord_mbox_id);

static void lsock_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags);

static void lsock_exit(struct result_code* rc);

struct itci_transport_apis lsock_trans_apis = {	lsock_locate_coord,
						lsock_init,
						lsock_exit,
						NULL,
						NULL,
						NULL,
						NULL,
						NULL,
						NULL };


/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
static bool lsock_locate_coord(struct result_code* rc, itc_mbox_id_t* my_mbox_id_in_itccoord, \
			itc_mbox_id_t* itccoord_mask, itc_mbox_id_t* itccoord_mbox_id)
{
	int sd, res, rx_len;
	struct sockaddr_un coord_addr;
	struct itc_locate_coord_request* lrequest;
	struct itc_locate_coord_reply* lreply;
	
	sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(sd < 0)
	{
		perror("\tDEBUG: lsock_locate_coord - socket");
		rc->flags |= ITC_SYSCALL_ERROR;
		return false;
	}

	memset(&coord_addr, 0, sizeof(struct sockaddr_un));
	coord_addr.sun_family = AF_LOCAL;
	strcpy(coord_addr.sun_path, ITC_ITCCOORD_FILENAME);

	res = connect(sd, (struct sockaddr*)&coord_addr, sizeof(coord_addr));
	if(res < 0)
	{
		perror("\tDEBUG: lsock_locate_coord - connect");
		rc->flags |= ITC_SYSCALL_ERROR;
		return false;
	}

	lrequest = (struct itc_locate_coord_request*)malloc(sizeof(struct itc_locate_coord_request));
	if(lrequest == NULL)
	{
		perror("\tDEBUG: lsock_locate_coord - malloc");
		rc->flags |= ITC_SYSCALL_ERROR;
		return false;
	}

	lrequest->msgno		= ITC_LOCATE_COORD_REQUEST;
	lrequest->my_pid	= getpid();

	res = send(sd, lrequest, sizeof(struct itc_locate_coord_request), 0);
	free(lrequest);
	if(res < 0)
	{
		perror("\tDEBUG: lsock_locate_coord - send");
		rc->flags |= ITC_SYSCALL_ERROR;
		return false;
	}

	lreply = (struct itc_locate_coord_reply*)malloc(RXBUF_LEN);
	if(lreply == NULL)
	{
		perror("\tDEBUG: lsock_locate_coord - malloc");
		rc->flags |= ITC_SYSCALL_ERROR;
		return false;
	}

	rx_len = recv(sd, lreply, RXBUF_LEN, 0);
	if(rx_len < (int)sizeof(struct itc_locate_coord_reply))
	{
		printf("\tDEBUG: lsock_locate_coord - Message received too small, rx_len = %u!\n", rx_len);
		free(lreply);
		rc->flags |= ITC_INVALID_MSG_SIZE;
		return false;
	}
	close(sd);

	/* Done communication, start analyzing the response */
	if(lreply->msgno == ITC_LOCATE_COORD_REPLY && lreply->my_mbox_id_in_itccoord != ITC_NO_MBOX_ID)
	{
		if(lreply->my_mbox_id_in_itccoord != ITC_NO_MBOX_ID)
		{
			*my_mbox_id_in_itccoord 	= lreply->my_mbox_id_in_itccoord;
			*itccoord_mask			= lreply->itccoord_mask;
			*itccoord_mbox_id		= lreply->itccoord_mbox_id;
			lsock_inst.is_coord_running	= true;
		} else
		{
			/* Indicate that no more process can be added, limited number of processes 255 has been reached */
			printf("\tDEBUG: lsock_locate_coord - No more process can be added by itccoord!\n");
			rc->flags |= ITC_OUT_OF_RANGE;
			free(lreply);
			return false;
		}
		
	} else
	{
		/* Indicate that we have received a strange message type in response */
		printf("\tDEBUG: lsock_locate_coord - Unknown message received!\n");
		rc->flags |= ITC_INVALID_ARGUMENTS;
		free(lreply);
		return false;
	}

	free(lreply);
	return true;
}

static void lsock_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      	int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;
	(void)flags;
	(void)itccoord_mask;

	int sd, res, rx_len;
	struct sockaddr_un coord_addr;
	char* str_ack;

	if(lsock_inst.is_coord_running)
	{
		printf("\tDEBUG: lsock_init - itccoord is running!\n");
		if(!lsock_inst.is_path_created)
		{
			printf("\tDEBUG: lsock_init - lsockpath not created yet, create it!\n");
			generate_lsockpath(rc);
			if(rc->flags != ITC_OK)
			{
				printf("\tDEBUG: lsock_init - Failed to generate lsockpath!\n");
				return;
			}
		}	

		sd = socket(AF_LOCAL, SOCK_STREAM, 0);
		if(sd < 0)
		{
			perror("\tDEBUG: lsock_init - socket");
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		memset(&coord_addr, 0, sizeof(struct sockaddr_un));
		coord_addr.sun_family = AF_LOCAL;
		sprintf(coord_addr.sun_path, "%s_0x%08x", ITC_LSOCKET_FILENAME, my_mbox_id_in_itccoord);

		// When using connect() remember must use sizeof(coord_addr) here.
		// If you use sizeof(struct sockaddr) instead, there will be a bug that no such file or directory even though it's existing, Idk why!!!
		res = connect(sd, (struct sockaddr*)&coord_addr, sizeof(coord_addr));
		if(res < 0)
		{
			printf("\tDEBUG: lsock_init - Failed to connect to address %s, res = %d, errno = %d!\n", coord_addr.sun_path, res, errno);
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		str_ack = (char *)malloc(4); // Only enough space for 'a' 'c' 'k' '\0'
		do
		{
			/* When this socket file descriptor is created, itccoord will scan it and try to connect to it, and then send back to us an string "ack" */
			rx_len = recv(sd, str_ack, 4, 0);
		} while (rx_len < 0 && errno == EINTR);
		
		if(rx_len < 0)
		{
			printf("\tDEBUG: lsock_init - ACK from itccoord not received, rx_len = %u!\n", rx_len);
			rc->flags |= ITC_SYSCALL_ERROR;
		} else if(rx_len != 4 && strcmp("ack", str_ack) != 0)
		{
			rc->flags |= ITC_INVALID_RESPONSE;
		}

		lsock_inst.sd = sd;
		free(str_ack);
	}
}

static void lsock_exit(struct result_code* rc)
{
	int res;

	if(lsock_inst.is_coord_running)
	{
		res = close(lsock_inst.sd);
		if(res < 0)
		{
			perror("\tDEBUG: lsock_exit - close");
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}
	}

	memset(&lsock_inst, 0, sizeof(struct lsock_instance));
}



/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void generate_lsockpath(struct result_code* rc)
{
	int res;

	res = mkdir(ITC_SOCKET_FOLDER, 0777);

	if(res < 0 && errno != EEXIST)
	{
		perror("\tDEBUG: generate_lsockpath - mkdir 2");
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	} else if(res < 0 && errno == EEXIST)
	{
		printf("\tDEBUG: generate_lsockpath - lsockpath %s already exists!\n", ITC_SOCKET_FOLDER);
	}

	res = chmod(ITC_SOCKET_FOLDER, 0777);
	if(res < 0)
	{
		printf("\tDEBUG: generate_lsockpath - Failed to chmod %s, errno = %d!\n", ITC_SOCKET_FOLDER, errno);
		return;
	}

	lsock_inst.is_path_created = true;
}