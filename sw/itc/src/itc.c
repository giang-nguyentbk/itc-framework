/* Until now, everything, at least local/sysvmq/lsock transportation, is ready. 
* So will moving on to itc.c implementation - the main function of ITC system */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <search.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "itc.h"
#include "itc_impl.h"
#include "itci_alloc.h"
#include "itci_trans.h"
#include "itc_threadmanager.h"
#include "itc_proto.h"



/*****************************************************************************\/
*****                      INTERNAL TYPES IN ITC.C                         *****
*******************************************************************************/
// union itc_msg {
// 	uint32_t				msgno;

// };

struct itc_instance {
	uint32_t			flags;
	
};

/*****************************************************************************\/
*****                     INTERNAL VARIABLES IN ITC.C                      *****
*******************************************************************************/



/*****************************************************************************\/
*****                   INTERNAL FUNCTIONS PROTOTYPES                      *****
*******************************************************************************/


/*****************************************************************************\/
*****                   TRANS INTERFACE IMPLEMENTATION                     *****
*******************************************************************************/



/*****************************************************************************\/
*****                        FUNCTION DEFINITIONS                          *****
*******************************************************************************/



/*****************************************************************************\/
*****                  INTERNAL FUNCTIONS IMPLEMENTATION                   *****
*******************************************************************************/
