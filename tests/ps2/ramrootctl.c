/* Deliberate convenience tools for the development RAM root. */
#include <sys/reboot.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/');
	name = name ? name + 1 : argv[0];
	(void)argc;
	sync();
	if (strcmp(name, "sync") == 0)
		return 0;
	reboot(strcmp(name, "halt") == 0 ? RB_HALT : RB_AUTOBOOT, NULL);
	perror("reboot");
	return 1;
}
