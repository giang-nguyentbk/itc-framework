/*
* _______________________   __________                                                    ______  
* ____  _/__  __/_  ____/   ___  ____/____________ _______ ___________      _________________  /__
*  __  / __  /  _  /        __  /_   __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* __/ /  _  /   / /___      _  __/   _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* /___/  /_/    \____/      /_/      /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                 
*/

#ifndef __ITC_PROTO_H__
#define __ITC_PROTO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "itc.h"
#include "itc_impl.h"

#define ITC_PROTO_MSG_BASE			(ITC_MSG_BASE + 0x500)

#define ITC_LOCATE_COORD_REQUEST		(ITC_PROTO_MSG_BASE + 0x1)
struct itc_locate_coord_request {
	uint32_t	msgno;
	pid_t		my_pid;
};

#define ITC_LOCATE_COORD_REPLY			(ITC_PROTO_MSG_BASE + 0x2)
struct itc_locate_coord_reply {
	uint32_t	msgno;
	itc_mbox_id_t	my_mbox_id_in_itccoord;
	itc_mbox_id_t	itccoord_mask;
	itc_mbox_id_t	itccoord_mbox_id;
};

#define ITC_NOTIFY_COORD_ADD_MBOX		(ITC_PROTO_MSG_BASE + 0x3)
#define ITC_NOTIFY_COORD_RMV_MBOX		(ITC_PROTO_MSG_BASE + 0x4)
struct itc_notify_coord_add_rmv_mbox {
	uint32_t	msgno;
	itc_mbox_id_t	mbox_id;
	char		mbox_name[1];
};

#define ITC_LOCATE_MBOX_SYNC_REQUEST		(ITC_PROTO_MSG_BASE + 0x5)
struct itc_locate_mbox_sync_request {
	uint32_t	msgno;
	itc_mbox_id_t	from_mbox;
	int32_t		timeout;
	bool		find_only_internal;
	char		mbox_name[1];
};

#define ITC_LOCATE_MBOX_SYNC_REPLY		(ITC_PROTO_MSG_BASE + 0x6)
struct itc_locate_mbox_sync_reply {
	uint32_t	msgno;
	itc_mbox_id_t	mbox_id;
	pid_t		pid;
	bool		is_external;
	char		namespace[1];
};

#define ITC_FWD_DATA_TO_ITCGWS			(ITC_PROTO_MSG_BASE + 0x7)
struct itc_fwd_data_to_itcgws {
	uint32_t	msgno;
	char		to_namespace[ITC_MAX_NAME_LENGTH];
	uint32_t	payload_length;
	char		payload[1]; // Flatten whole itc_message data into a serial of bytes
};

#define ITC_GET_NAMESPACE_REQUEST		(ITC_PROTO_MSG_BASE + 0x8)
struct itc_get_namespace_request {
	uint32_t	msgno;
	itc_mbox_id_t	mbox_id;
};

#define ITC_GET_NAMESPACE_REPLY			(ITC_PROTO_MSG_BASE + 0x9)
struct itc_get_namespace_reply {
	uint32_t	msgno;
	char		namespace[1];
};

#define ITC_LOCATE_MBOX_FROM_ITCGWS_REQUEST	(ITC_PROTO_MSG_BASE + 0xA)
struct itc_locate_mbox_from_itcgws_request {
	uint32_t	msgno;
	itc_mbox_id_t	itccoord_mboxid;
	char		mboxname[1];
};

#define ITC_LOCATE_MBOX_FROM_ITCGWS_REPLY	(ITC_PROTO_MSG_BASE + 0xB)
struct itc_locate_mbox_from_itcgws_reply {
	uint32_t	msgno;
	itc_mbox_id_t	mbox_id;
	char		namespace[1];
};


#ifdef __cplusplus
}
#endif

#endif // __ITC_PROTO_H__