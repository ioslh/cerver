#ifndef sbuf_h
#define sbuf_h
#include <fcntl.h>
#include <stdlib.h>
#include <semaphore.h>


typedef struct {
    int *buf;
    int capacity;
    int head;
    int tail;
    sem_t *items;
    sem_t *mutex;
    sem_t *slots;
} sbuf_t;

void sbuf_init(sbuf_t *, int);
void sbuf_insert(sbuf_t *, int);
int sbuf_delete(sbuf_t *);
void sbuf_destroy(sbuf_t *);
void print_sbuf(char *, sbuf_t *);
#endif /* sbuf_h */
