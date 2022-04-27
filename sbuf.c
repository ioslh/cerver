#include "sbuf.h"

void sbuf_init(sbuf_t *buf, int n) {
  buf->capacity = n;
  buf->buf = (int *)calloc(n, sizeof(int));
  if (!buf->buf) fatal(1, "Failed calloc buf");
  buf->head = buf->tail = 0;
  buf->conns = sem_open("s_conn_lock", O_CREAT, 0644, 0);
  buf->mutex = sem_open("s_buf_lock", O_CREAT, 0644, 1);
  buf->slots = sem_open("s_slots_lock", O_CREAT, 0644, n);
}

void sbuf_insert(sbuf_t *buf, int connfd) {
  sem_wait(buf->slots);
  sem_wait(buf->mutex);
  buf->buf[buf->tail] = connfd;
  buf->tail = (buf->tail + 1) % buf->capacity;
  sem_post(buf->conns);
  sem_post(buf->mutex);
}

int sbuf_delete(sbuf_t *buf) {
  int connfd;
  sem_wait(buf->conns);
  sem_wait(buf->mutex);
  buf->head = (buf->head + 1) % buf->capacity;
  connfd = buf->buf[buf->head];
  sem_post(buf->slots);
  sem_post(buf->mutex);
  return connfd;
}

void sbuf_destroy(sbuf_t *buf) {
  free(buf->buf);
}