#pragma once
#include <stdio.h>
#include <stdlib.h>
#define VERIFICAR(cond)                                                                   \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            exit(1);                                                                      \
        }                                                                                 \
    } while (0)
