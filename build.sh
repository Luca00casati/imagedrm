#!/bin/sh
gcc -z noexecstack -o main main.c -Wall -Wextra -ldrm -lm -O2 -s
