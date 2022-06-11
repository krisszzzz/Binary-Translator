#ifndef LOG_INCLUDED
#define LOG_INCLUDED
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <execinfo.h>

#define RED "\u001b[31m"

#define FATAL_RED "\u001b[31;1m"
#define GREEN "\u001b[32m"
#define YELLOW "\u001b[33m"
#define BLUE "\u001b[34m"
#define MAGENTA "\u001b[35m"
#define CYAN    "\u001b[36m"
#define END  "\u001b[0m"

#define ERROR(condition, ret_val,...)                       \
    if(condition) {                                         \
        ErrorPrint(__VA_ARGS__);                            \
        return ret_val;                                     \
    }

#define RET_IF(condition, ret_val)               \
if(condition) {                                  \
    return ret_val;                              \
}                                        

void  SetLogFile(FILE* log_file = nullptr);

void  ResetLogFile();

void  ResetAllLogFiles();

int   PrintToLog(const char* format, ...);

FILE* GetCurrentLogFile();

#define ErrorPrint(...)                                               \
ErrorPrint_(__PRETTY_FUNCTION__, __LINE__, __FILE__, __VA_ARGS__); 

#define PrettyPrint(...)                                                 \
PrettyPrint_(__PRETTY_FUNCTION__, __LINE__, __FILE__, __VA_ARGS__);

int PrettyPrint_(const char* function, const int line, const char* file, const char* format, ...);

int ErrorPrint_(const char* function, const int line, const char* file, const char* format, ...);

#endif
