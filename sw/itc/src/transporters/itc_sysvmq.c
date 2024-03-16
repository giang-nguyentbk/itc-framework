

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include <errno.h>
#include <search.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_trans.h"


/*****************************************************************************\/
*****                    INTERNAL TYPES IN SYSV-ATOR                       *****
*******************************************************************************/
#ifndef ITC_SYSVMSQ_PATH
#define ITC_SYSVMSQ_PATH "/tmp/itc/sysvmsq/"
#endif

#ifndef ITC_SYSVMSQ_FILENAME
#define ITC_SYSVMSQ_FILENAME "/tmp/itc/sysvmsq/sysvmsq_file"
#endif

struct sysvmq_contactlist {
	itc_mbox_id_t	mbox_id_in_itccoord;
	int		sysvmq_id;
};

struct sysvmq_instance {
	itc_mbox_id_t           	my_mbox_id_in_itccoord;
        itc_mbox_id_t           	itccoord_mask;
        itc_mbox_id_t           	itccoord_shift;

	itc_mbox_id_t			my_mbox_id;
	int				my_sysvmq_id;

	pid_t				pid;
	pthread_mutex_t			thread_mtx;
	pthread_key_t			destruct_key;

	int				is_initialized;
	int				is_terminated;
	int				max_msgsize;
	char*				rx_buffer;

	struct sysvmq_contactlist	sysvmq_cl[MAX_SUPPORTED_PROCESSES];
};



/*****************************************************************************\/
*****                  INTERNAL VARIABLES IN LOCAL-ATOR                    *****
*******************************************************************************/
static struct sysvmq_instance sysvmq_inst; // One instance per a process, multiple threads all use this one.




/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/
static void release_sysvmq_resources(struct result_code* rc);
static void generate_msqfile(struct result_code* rc);






/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/
static void sysvmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags);

static void sysvmq_exit(struct result_code* rc);


static void sysvmq_send(struct result_code* rc, struct itc_message *message, itc_mbox_id_t to);

static int sysvmq_maxmsgsize(struct result_code* rc);

static void* sysvmq_rx_thread(void *data);

struct itci_transport_apis sysvmq_trans_apis = { NULL,
                                            	sysvmq_init,
                                            	sysvmq_exit,
                                            	NULL,
                                            	NULL,
                                            	sysvmq_send,
                                            	NULL,
                                            	NULL,
                                            	sysvmq_maxmsgsize };




/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/
void sysvmq_init(struct result_code* rc, itc_mbox_id_t my_mbox_id_in_itccoord, itc_mbox_id_t itccoord_mask, \
		      int nr_mboxes, uint32_t flags)
{
	(void)nr_mboxes;

	if(sysvmq_inst.is_initialized != 0)
	{
		if(flags & ITC_FLAGS_FORCE_REINIT)
		{
			release_sysvmq_resources(rc);
			if(rc != ITC_OK)
			{
				return;
			}
			(void)sysvmq_maxmsgsize(rc);
		} else
		{
			rc->flags |= ITC_ALREADY_INIT;
			return;
		}
	}

	generate_msqfile(rc);
	if(rc->flags != ITC_OK)
	{
		return;
	}

	// Calculate itccoord_shift value
	int tmp_shift, tmp_mask;
	tmp_shift = 0;
	tmp_mask = itccoord_mask;
	while(!(tmp_mask & 0x1))
	{
		tmp_mask = tmp_mask >> 1;
		tmp_shift = tmp_shift++; // Should be 20 currently
	}

	sysvmq_inst.my_sysvmq_id		= -1; // Will be only specified when rx thread starts
	sysvmq_inst.pid				= getpid();
	sysvmq_inst.itccoord_mask 		= itccoord_mask;
	sysvmq_inst.itccoord_shift		= tmp_shift;
	sysvmq_inst.my_mbox_id_in_itccoord	= my_mbox_id_in_itccoord;

	// Create key for thread-specific data (mbox_id)
	if(pthread_key_create(&sysvmq_inst.destruct_key, rxthread_destructor) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	if(pthread_mutex_init(&sysvmq_inst.thread_mtx, NULL) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	
}

static int sysvmq_maxmsgsize(struct result_code* rc)
{
	// struct msginfo from <bits/msg.h> included in <sys/msg.h>
	struct msginfo info;

	if(sysvmq_inst.max_msgsize > 0)
	{
		return sysvmq_inst.max_msgsize;
	}

	if(msgctl(0, IPC_INFO, (struct msqid_ds*)&info) == -1)
	{
		// ERROR tracing is needed only, no need to set result code
		rc->flags |= ITC_SYSCALL_ERROR;
	}

	sysvmq_inst.max_msgsize = MIN(info.msgmax, info.msgmnb);

	return sysvmq_inst.max_msgsize;
}





/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
static void release_sysvmq_resources(struct result_code* rc)
{
	free(sysvmq_inst.sysvmq_file);

	if(pthread_key_delete(sysvmq_inst.destruct_key) != 0)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	memset(&sysvmq_inst, 0, sizeof(struct sysvmq_instance));
}

static int generate_msqfile(struct result_code* rc)
{
	FILE* fd;
	int res;

	res = mkdir(ITC_SYSVMSQ_PATH, 0777);

	if(res < 0 && errno != EEXIST)
	{
		rc->flags |= ITC_SYSCALL_ERROR;
		return;
	}

	if(errno != EEXIST)
	{
		res = chmod(ITC_SYSVMSQ_PATH, 0777);
		if(res < 0)
		{
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		fd = fopen(ITC_SYSVMSQ_FILENAME, "w");
		if(fd == NULL)
		{
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}

		if(fclose(fd) != 0)
		{
			rc->flags |= ITC_SYSCALL_ERROR;
			return;
		}
	}

	sysvmq_inst.is_initialized = 1;
}