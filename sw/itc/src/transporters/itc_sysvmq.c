

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>
#include <search.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"


/*****************************************************************************\/
*****                    INTERNAL TYPES IN SYSV-ATOR                       *****
*******************************************************************************/

struct sysvmq_instance {
	itc_mbox_id_t           my_mbox_id_in_itccoord;
        itc_mbox_id_t           itccoord_mask;
        itc_mbox_id_t           itccoord_shift;

	itc_mbox_id_t		mbox_id;
	int			sysvmq_id;

	pid_t			pid;
	pthread_mutex_t		thread_mtx;
	pthread_key_t		destruct_key;

	int			is_terminated;
	int			max_msgsize;
	char*			itccoord_dir_path;
};

/*****************************************************************************\/
*****                    INTERNAL TYPES IN LOCAL-ATOR                      *****
*******************************************************************************/