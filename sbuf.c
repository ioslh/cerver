#include "sbuf.h"
#include "utils.h"

void sbuf_init(sbuf_t *sp, int n) {
  sp->capacity = n;
  sp->buf = (int *)calloc(n, sizeof(int));
  if (!sp->buf) fatal_exit(1, "Failed calloc buf");
  sp->head = sp->tail = 0;
  sp->items = sem_open("s_conn_lock", O_CREAT, 0644, 0);
  sp->mutex = sem_open("s_buf_lock", O_CREAT, 0644, 1);
  sp->slots = sem_open("s_slots_lock", O_CREAT, 0644, n);
}

void sbuf_insert(sbuf_t *sp, int connfd) {
  sem_wait(sp->slots);
  sem_wait(sp->mutex);
  sp->buf[sp->tail] = connfd;
  sp->tail = (sp->tail + 1) % sp->capacity;
  // print_sbuf("insert", sp);
  sem_post(sp->items);
  sem_post(sp->mutex);
}

int sbuf_delete(sbuf_t *sp) {
  int connfd;
  sem_wait(sp->items);
  sem_wait(sp->mutex);
  connfd = sp->buf[sp->head];
  sp->head = (sp->head + 1) % sp->capacity;
  // print_sbuf("delete", sp);
  sem_post(sp->slots);
  sem_post(sp->mutex);
  return connfd;
}

void sbuf_destroy(sbuf_t *sp) {
  free(sp->buf);
}

void print_sbuf(char *action, sbuf_t *sp) {
  int i;
  printf("%s sbuf[", action);
  for(i = 0; i < sp->capacity; i++) {
    printf(" %d ", sp->buf[i]);
  }
  printf("], head = %d, tail = %d\n", sp->head, sp->tail);
}