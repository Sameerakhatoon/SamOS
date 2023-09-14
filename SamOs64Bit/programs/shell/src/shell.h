#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

struct command_argument;
int shell_execute(struct command_argument* command);

#endif
