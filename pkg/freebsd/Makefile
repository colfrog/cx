# Created by: Laurent Cimon <laurent@nilio.ca>
# $FreeBSD$

PORTNAME=	cx
PORTVERSION=	0.2
PORTREVISION=	1
CATEGORIES=	sysutils
MASTER_SITES=	https://nilio.ca/cx/

MAINTAINER=	laurent@nilio.ca
COMMENT=	cx is a directory history management utility

LICENSE=	BSD2CLAUSE
LICENSE_FILE=	${WRKSRC}/LICENSE

LIB_DEPENDS=	libsqlite3.so:databases/sqlite3

USES=		gmake sqlite tar:xz

CFLAGS+= -I${LOCALBASE}/include -L${LOCALBASE}/lib

post-install:
	${STRIP_CMD} ${STAGEDIR}${PREFIX}/bin/cxc
	${STRIP_CMD} ${STAGEDIR}${PREFIX}/bin/cxd

.include <bsd.port.mk>
