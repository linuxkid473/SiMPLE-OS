/*
 * cmd.c — SiMPLE OS port.
 *
 * Upstream neatvi runs `:!cmd` pipelines through fork + execvp("/bin/sh")
 * and talks to editor servers over UNIX-domain sockets.  SiMPLE OS has
 * neither a shell nor sockets, so these entry points fail gracefully:
 * ex.c already treats a NULL return as "command produced no output" and
 * shows the ex message line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vi.h"

/* Execute cmd; ibuf is its stdin, return its output if oproc != 0. */
char *cmd_pipe(char *cmd, char *ibuf, int oproc)
{
	(void) cmd;
	(void) ibuf;
	(void) oproc;
	ex_show("external commands are not available on SiMPLE OS");
	return NULL;
}

int cmd_exec(char *cmd)
{
	cmd_pipe(cmd, NULL, 0);
	return 1;
}

/* Send ibuf to the UNIX-domain socket at path and return the reply. */
char *cmd_unix(char *path, char *ibuf)
{
	(void) path;
	(void) ibuf;
	return NULL;
}
