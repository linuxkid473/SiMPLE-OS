#ifndef USER_MALLOC_H
#define USER_MALLOC_H

typedef unsigned int size_t;

void *malloc(size_t size);
void  free(void *ptr);

#endif
