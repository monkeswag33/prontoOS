#pragma once
#define MIN_LOG_LEVEL Debug

typedef enum log_level {
    Error,
    Warning,
    Info,
    Verbose,
    Debug,
    Trace
} log_level_t;

#ifdef __cplusplus
extern "C" {
#endif
void log(log_level_t log_level, const char*, const char*, ...);
#ifdef __cplusplus
}
#endif
