/*
*        ________________           ________                                                    ______  
* ____  ___  /___(_)__  /_______    ___  __/____________ _______ ___________      _________________  /__
* _  / / /  __/_  /__  /__  ___/    __  /_ __  ___/  __ `/_  __ `__ \  _ \_ | /| / /  __ \_  ___/_  //_/
* / /_/ // /_ _  / _  / _(__  )     _  __/ _  /   / /_/ /_  / / / / /  __/_ |/ |/ // /_/ /  /   _  ,<   
* \__,_/ \__/ /_/  /_/  /____/      /_/    /_/    \__,_/ /_/ /_/ /_/\___/____/|__/ \____//_/    /_/|_|  
*                                                                                                       
*/

#ifndef __TRACE_IF_H__
#define __TRACE_IF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

/* TPT = Trace-Point Tracking */

#define TRACE_INFO		1
#define TRACE_ERROR		2
#define TRACE_ABN		3
#define TRACE_DEBUG		4

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

void TPT_TRACE_ZZ(const char *file, int line, int level, const char *format, ...);

#define TPT_TRACE(level, format, ...) TPT_TRACE_ZZ(__FILENAME__, __LINE__, (level), (format), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // __TRACE_IF_H__