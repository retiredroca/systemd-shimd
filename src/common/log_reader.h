#ifndef LOG_READER_H
#define LOG_READER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct LogReader LogReader;

LogReader *log_reader_open(void);
void log_reader_close(LogReader *r);

int  log_reader_seek_tail(LogReader *r, int nlines);
int  log_reader_next_line(LogReader *r, char **line);
int  log_reader_wait(LogReader *r, int timeout_ms);

void log_reader_set_unit_filter(LogReader *r, const char *unit);
void log_reader_set_since(LogReader *r, const char *since);
void log_reader_set_until(LogReader *r, const char *until);

#endif
