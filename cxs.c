#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cx.h"

void
cx_set_path(char *path, bool absolute, char *buf, size_t buflen)
{
	char ibuf[buflen];
	memset(ibuf, 0, buflen);
	memset(buf, 0, buflen);

#ifdef CX_DATA_DIR
	const char *data_path = CX_DATA_DIR;
#else
	const char *data_path = getenv("XDG_DATA_HOME");
	if (data_path == NULL)
		data_path = "~/.local/share";
#endif

	if (absolute)
		strncpy(ibuf, path, buflen);
	else
		snprintf(ibuf, buflen, "%s/%s/%s", data_path, CX_DIR_NAME, path);

	if (ibuf[0] == '~')
		snprintf(buf, buflen, "%s%s", getenv("HOME"), ibuf + 1);
	else
		strncpy(buf, ibuf, buflen);

	buf[buflen - 1] = '\0';
	return;
}
