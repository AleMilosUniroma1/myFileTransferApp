#include <stdio.h>
#include <string.h>

#include "utils.h"

void *memcheck(const char *name, void *mem) {
  if (!mem) {
    perror(name);
    exit(EXIT_FAILURE);
  }
  return mem;
}

void *xmalloc(size_t size) { return memcheck("malloc", malloc(size)); }

void *xrealloc(void *old, size_t size) {
  return memcheck("realloc", realloc(old, size));
}

/* Wrapper of strdup -> Allocate and return string */
void *xstrdup(const char *s) { return memcheck("strdup", strdup(s)); }