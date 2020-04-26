PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
SHAREDDIR?=	${PREFIX}/share
CFLAGS+=	-O2 -pedantic -std=c99 -Wall -Werror \
		-I/usr/local/include -L/usr/local/lib

all: cxd cxc

CXS= cx.h cxs.c
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
	rm -f src/*.o cxd cxc
