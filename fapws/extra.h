#pragma once

#include <time.h>
#include <syslog.h> //loglevel: LOG_*

void stripChar(char *s, char *d, char c) ;

char * decode_uri(const char *uri);

char * remove_leading_and_trailing_spaces(char* s);

int str_append3(char *dest, char *src1, char *src2, char *src3, int n);

char *cur_time(char * fmt);

char *time_rfc1123(time_t t);

char *cur_time_rfc1123(void);

void log_mesg(unsigned int priority, const char* file, const char* func, int line, const char* fmt, ...) __attribute__((format (printf, 5, 6)));

extern int log_level;
#define LERROR(fmt, ...) do { if (log_level >= LOG_ERR)   log_mesg(LOG_ERR,   __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__); } while(0)
#define LINFO(fmt, ...)  do { if (log_level >= LOG_INFO)  log_mesg(LOG_INFO, NULL, NULL, -1, fmt, ## __VA_ARGS__); } while(0)
#define LDEBUG(fmt, ...) do { if (log_level >= LOG_DEBUG) log_mesg(LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__); } while(0)
