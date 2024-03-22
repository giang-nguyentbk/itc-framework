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

#define ITC_PROTO_MSG_BASE		(ITC_MSG_BASE + 0x500)

#define ITC_LOCATE_COORD_REQUEST	(ITC_PROTO_MSG_BASE + 0x1)
struct itc_locate_coord_request {
	uint32_t	msgno;
	pid_t		my_pid;
};

#define ITC_LOCATE_COORD_REPLY		(ITC_PROTO_MSG_BASE + 0x2)
struct itc_locate_coord_reply {
	uint32_t	msgno;
	itc_mbox_id_t	my_mbox_id_in_itccoord;
	itc_mbox_id_t	itccoord_mask;
	itc_mbox_id_t	itccoord_mbox_id;
};

#ifdef __cplusplus
}
#endif

#endif // __ITC_PROTO_H__