PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
SHAREDDIR?=	${PREFIX}/share
CFLAGS+=	-pedantic -std=c99 -Wall -Werror \
		-I/usr/local/include -L/usr/local/lib
CXCLIBS=
CXDLIBS=	-lm -lsqlite3

make:
	${CC} ${CFLAGS} -O2 cxs.c cxc.c ${CXCLIBS} -o cxc
	${CC} ${CFLAGS} -O2 cxs.c cxd.c ${CXDLIBS} -o cxd

debug:
	${CC} ${CFLAGS} -O0 -g cxs.c cxc.c ${CXCLIBS} -o cxc
	${CC} ${CFLAGS} -O0 -g cxs.c cxd.c ${CXDLIBS} -o cxd

install:
	mkdir -p ${BINDIR}
	mkdir -p ${SHAREDDIR}/cx
	install -m 755 cxc cxd ${BINDIR}
	install -m 644 cx.sh cx.zsh cx.fish cx.csh ${SHAREDDIR}/cx
