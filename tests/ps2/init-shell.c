/* Small RAM-root PID 1: establish a controlling SIO tty and supervise sh. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <ufs/ufs/ufsmount.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int
open_console(void)
{
	int fd = open("/dev/console", O_RDWR);
	if (fd < 0)
		return -1;
	for (int i = 0; i < 3; i++)
		if (dup2(fd, i) < 0)
			_exit(1);
	if (fd > 2)
		close(fd);
	return 0;
}

int
main(void)
{
	int status;
	pid_t child, pid;

	if (open_console() < 0)
		_exit(1);
	setvbuf(stdout, NULL, _IONBF, 0);
	struct ufs_args rootargs = { .fspec = NULL };
	if (mount(MOUNT_FFS, "/", MNT_UPDATE, &rootargs, sizeof(rootargs)) < 0) {
		perror("init: remount root read/write");
		_exit(1);
	}
	setenv("PATH", "/bin:/sbin:/usr/bin", 1);
	setenv("HOME", "/root", 1);
	setenv("TERM", "dumb", 1);
	setenv("PS1", "ps2# ", 1);
	chdir("/root");
	printf("PS2 RAM root: n32 shell, direct EE SIO, no SBIOS/RTE\n");
	for (;;) {
		child = fork();
		if (child == 0) {
			if (setsid() < 0 || ioctl(0, TIOCSCTTY, NULL) < 0) {
				perror("shell controlling tty");
				_exit(1);
			}
			execl("/bin/sh", "sh", "-i", (char *)NULL);
			perror("/bin/sh");
			_exit(127);
		}
		if (child < 0) {
			perror("fork shell");
			sleep(1);
			continue;
		}
		/* Reap orphaned children as well as the interactive shell. */
		do {
			pid = wait(&status);
		} while (pid != child && (pid >= 0 || errno == EINTR));
		/* Session exit revokes its tty vnodes, including inherited fds. */
		while (open_console() < 0)
			sleep(1);
		printf("init: shell ended; restarting\n");
		sleep(1);
	}
}
