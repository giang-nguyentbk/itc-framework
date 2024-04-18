
/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

#ifndef __ITC_GW_PROTO_H__
#define __ITC_GW_PROTO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <netinet/in.h>

#include "itc_impl.h"

#define ITCGW_PAYLOAD_TYPE_BASE			0x100

#define ITCGW_GET_NAMESPACE_REQUEST		(ITCGW_PAYLOAD_TYPE_BASE + 0x1)
struct itcgw_get_namespace_request {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
};

#define ITCGW_GET_NAMESPACE_REPLY		(ITCGW_PAYLOAD_TYPE_BASE + 0x2)
struct itcgw_get_namespace_reply {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		namespace[1];
};

#define ITCGW_UDP_ADD_PEER			(ITCGW_PAYLOAD_TYPE_BASE + 0x3)
#define ITCGW_UDP_RMV_PEER			(ITCGW_PAYLOAD_TYPE_BASE + 0x4)
struct itcgw_udp_add_rmv_peer {
	uint32_t	msgno;
	char		addr[1];
};

#define ITCGW_ITC_DATA_FWD			(ITCGW_PAYLOAD_TYPE_BASE + 0x5)
struct itcgw_itc_data_fwd {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	uint32_t	payload_length;
	char		payload[1];
};

#define ITCGW_LOCATE_MBOX_REQUEST		(ITCGW_PAYLOAD_TYPE_BASE + 0x6)
struct itcgw_locate_mbox_request {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		mboxname[1];
};

#define ITCGW_LOCATE_MBOX_REPLY			(ITCGW_PAYLOAD_TYPE_BASE + 0x7)
struct itcgw_locate_mbox_reply {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	itc_mbox_id_t	mbox_id;
};


#define ITCGW_MAX_PAYLOAD_TYPE			ITCGW_ITC_MSG_FWD_REPLY


typedef enum {
	ITCGW_STATUS_OK = 0,
	ITCGW_INVALID_TYPE,
	ITCGW_NUM_OF_STATUS
} status_e;


struct itcgw_header {
	uint32_t	sender;
	uint32_t	receiver;
	uint32_t	protRev;
	uint32_t	msgno;
	uint32_t	payloadLen;
};

struct itcgw_msg {
	struct itcgw_header					header;
	union {
		// uint32_t					payload_startpoint;
		uint32_t					errorcode;
		struct itcgw_get_namespace_request		itcgw_get_namespace_request;
		struct itcgw_get_namespace_reply		itcgw_get_namespace_reply;
		struct itcgw_itc_data_fwd			itcgw_itc_data_fwd;
		struct itcgw_locate_mbox_request		itcgw_locate_mbox_request;
		struct itcgw_locate_mbox_reply			itcgw_locate_mbox_reply;
	} payload;
};


#ifdef __cplusplus
}
#endif

#endif // __ITC_GW_PROTO_H__