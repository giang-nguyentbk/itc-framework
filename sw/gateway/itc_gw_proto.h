
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

#define ITCGW_PAYLOAD_TYPE_BASE			0x100

#define ITCGW_GET_NAMESPACE_REQUEST		(ITCGW_PAYLOAD_TYPE_BASE + 0x1)
struct itcgw_get_namespace_request {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		namespace[1];
};

#define ITCGW_GET_NAMESPACE_REPLY		(ITCGW_PAYLOAD_TYPE_BASE + 0x2)
struct itcgw_get_namespace_reply {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		namespace[1];
};

#define ITCGW_ITC_DATA_FWD			(ITCGW_PAYLOAD_TYPE_BASE + 0x3)
struct itcgw_itc_data_fwd {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	uint32_t	payload_length;
	char		payload[1];
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
	} payload;
};


#ifdef __cplusplus
}
#endif

#endif // __ITC_GW_PROTO_H__