PREFIX?=	/usr/local
BINDIR?=	${DESTDIR}${PREFIX}/bin
SHAREDDIR?=	${DESTDIR}${PREFIX}/share
CFLAGS+=	-O2 -pedantic -std=c99 -Wall -Werror

all: build
build: cxd cxc

CXS= cxs.c
cxd: LDFLAGS+= -lm -lsqlite3
cxd: ${CXS} cxd.c
cxc: ${CXS} cxc.c

debug: CFLAGS+= -g -O0
debug: cxd cxc

BINDINGS=	lib/cx.sh lib/cx.zsh lib/cx.fish lib/cx.csh
install:
	mkdir -p ${BINDIR}
	mkdir -p ${SHAREDDIR}/cx
	install -m 755 cxc cxd ${BINDIR}
	install -m 644 ${BINDINGS} ${SHAREDDIR}/cx

clean:
	rm -f *.o cxd cxc
