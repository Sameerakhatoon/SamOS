#ifndef SAMTCPIP_LOG_H
#define SAMTCPIP_LOG_H

// ch37: log-level discipline.
//
// Four levels (low to high): DEBUG < INFO < WARN < ERROR.
// `g_log_level` filters: messages below the threshold are
// silently dropped.
//
// Default level is INFO. The thousands of existing printf()
// calls in the codebase are treated as INFO-level (they were
// written before this chapter and stay as-is). New code uses
// the LOG_*() macros so it can be silenced.
//
// In the kernel the default is WARN, so the per-packet
// INFO lines stay off the serial log; samos.net_debug sets DEBUG. The
// macros print through samtcpip_logf (port.h), which does not filter
// again; printf is port.h's samtcpip_printf, filtered as INFO.

#include "port.h"

#define LOG_LEVEL_DEBUG  0
#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_ERROR  3

extern int g_log_level;

void log_set_level_by_name(const char* name);

#define LOG_DEBUG(...) do { if(g_log_level <= LOG_LEVEL_DEBUG){ samtcpip_logf("[D] "); samtcpip_logf(__VA_ARGS__); } } while(0)
#define LOG_INFO(...)  do { if(g_log_level <= LOG_LEVEL_INFO ){ samtcpip_logf("[I] "); samtcpip_logf(__VA_ARGS__); } } while(0)
#define LOG_WARN(...)  do { if(g_log_level <= LOG_LEVEL_WARN ){ samtcpip_logf("[W] "); samtcpip_logf(__VA_ARGS__); } } while(0)
#define LOG_ERROR(...) do { if(g_log_level <= LOG_LEVEL_ERROR){ samtcpip_logf("[E] "); samtcpip_logf(__VA_ARGS__); } } while(0)

#endif
