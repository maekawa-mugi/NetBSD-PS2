/* R5900 is uniprocessor and has no LL/SC. Never fall back to LL/SC locks. */
#include <sys/types.h>
#include <sys/ras.h>
#include <unistd.h>
#include "extern.h"

RAS_DECL(_r5900_cas32);
RAS_DECL(_r5900_cas64);

void __section(".text.startup") __attribute__((__visibility__("hidden")))
__libc_atomic_init(void)
{
	if (rasctl(RAS_ADDR(_r5900_cas32), RAS_SIZE(_r5900_cas32), RAS_INSTALL)
	    == -1 ||
	    rasctl(RAS_ADDR(_r5900_cas64), RAS_SIZE(_r5900_cas64), RAS_INSTALL)
	    == -1) {
		static const char msg[] = "libc: R5900 atomic RAS installation failed\n";
		(void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
		_exit(127);
	}
}
