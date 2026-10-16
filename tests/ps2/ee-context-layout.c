/* Keep the standalone assembly test's ABI constants tied to real headers. */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <sys/stddef.h>
__CTASSERT(sizeof(ucontext_t) == 952);
__CTASSERT(sizeof(siginfo_t) == 128);
__CTASSERT(sizeof(struct sigaction) == 24);
__CTASSERT(offsetof(struct sigaction, sa_flags) == 20);
__CTASSERT(offsetof(ucontext_t, uc_mcontext.__ee_hi) == 608);
__CTASSERT(offsetof(ucontext_t, uc_mcontext.__ee_sa) == 880);
__CTASSERT(offsetof(ucontext_t, uc_mcontext.__gregs[_REG_EPC]) == 320);
