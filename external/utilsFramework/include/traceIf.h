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

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

void LOG_INFO_ZZ(const char *file, int line, const char *format, ...);
void LOG_ERROR_ZZ(const char *file, int line, const char *format, ...);
void LOG_ABN_ZZ(const char *file, int line, const char *format, ...);
void LOG_DEBUG_ZZ(const char *file, int line, const char *format, ...);

#define LOG_INFO(format, ...) LOG_INFO_ZZ(__FILENAME__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG_ERROR_ZZ(__FILENAME__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ABN(format, ...) LOG_ABN_ZZ(__FILENAME__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG_DEBUG_ZZ(__FILENAME__, __LINE__, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // __TRACE_IF_H__