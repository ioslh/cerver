PROG= cerver
CFLAGS= -Wall -Werror -Wextra
OBJS= cerver.o core.o http.o sock.o rio.o utils.o sbuf.o

.c.o:
	${CC} ${CFLAGS} -c $< -o $@

all: ${PROG}
depend:
	mkdep -- ${CFLAGS} *.c

${PROG}: ${OBJS}
	@echo $@ depends on $?
	${CC} ${OBJS} -o ${PROG} ${LDFLAGS}

clean:
	rm -f ${PROG} ${OBJS}
