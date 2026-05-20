#ifndef BACKEND_HELPERS_H
#define BACKEND_HELPERS_H

#include <stddef.h>

int  run_cmd(const char *argv[]);
int  run_cmd_capture(const char *argv[], char **output, size_t *output_len);
void chomp(char *s);
char *strip_unit_suffix(const char *name);

#endif
