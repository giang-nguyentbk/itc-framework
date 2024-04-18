#include <stdint.h>
#include <netinet/in.h>


#define PAYLOAD_TYPE_BASE			0x100

#define GET_NAMESPACE_REQUEST			(PAYLOAD_TYPE_BASE + 0x1)
struct get_namespace_request {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		namespace[1];
};

#define GET_NAMESPACE_REPLY			(PAYLOAD_TYPE_BASE + 0x2)
struct get_namespace_reply {
	// uint32_t	payload_startpoint;
	uint32_t	errorcode;
	char		namespace[1];
};

typedef enum {
	STATUS_OK = 0,
	INVALID_TYPE,
	NUM_OF_STATUS
} status_e;


struct msg_header {
	// uint32_t	sender;
	// uint32_t	receiver;
	// uint32_t	protRev;
	uint32_t	msgno;
	uint32_t	payloadLen;
};

struct tcp_msg {
	struct msg_header					header;
	union {
		// uint32_t					payload_startpoint;
		uint32_t					errorcode;
		struct get_namespace_request			get_namespace_request;
		struct get_namespace_reply			get_namespace_reply;
		// struct itcgw_itc_data_fwd			itcgw_itc_data_fwd;
	} payload;
};