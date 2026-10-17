#!/usr/bin/env python3
"""Exercise the production EE relay's handshake, lost edges and stop lifetime."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'sys/arch/playstation2/ee/sif_native.c').read_text()
section = source[source.index('/* The native profile exclusively owns'):source.index('static int\nps2sif_match')]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include "usb_irq.h"
#define PS2_IOP_BOOTSTRAP
#define SIF_SMFLAG 0x1000f230U
#define I_CH1_SBUS 1
#define IPL_BIO 5
static uint32_t flag, enable, disable;
#define REG(a) (*((a)==SIF_SMFLAG ? &flag : (a)==0x1f801610 ? &enable : &disable))
static struct ps2_usb_irq_mailbox ram;
static unsigned allocs, calls, removed;
static int exec_error, entry_result;
static bool inject, stop_in_handler;
static int splbio(void) { return 0; }
static void splx(int s) { (void)s; }
static void barrier(void) {}
#define aprint_error printf
struct ps2sif_module_result { int module_id, entry_result, start_result; };
static const uint32_t ps2_iop_usb_irq_image[16];
static volatile uint32_t *iop_window(uint32_t a) {
    assert(a==0x10000); return (volatile uint32_t *)&ram;
}
static int ps2sif_alloc(size_t n,uint32_t *a) {
    assert(n==64); allocs++; *a=0x10000; return 0;
}
static void *intc_intr_establish(int ch,int ipl,int (*fn)(void *),void *p) {
    assert(ch==1 && ipl==5 && fn && !p); return (void *)1;
}
static void intc_intr_disestablish(void *p) { assert(p==(void *)1); removed++; }
static int ps2sif_exec_irx(const uint32_t *p,size_t n,const char *arg,size_t len,
    struct ps2sif_module_result *r) {
    assert(p==ps2_iop_usb_irq_image && n==sizeof(ps2_iop_usb_irq_image));
    assert(len==9 && arg[0]=='0');
    ram.magic=PS2_USB_IRQ_MAGIC; ram.sequence=1; r->entry_result=entry_result;
    r->module_id=r->start_result=26;
    return exec_error;
}
void ps2sif_usb_intr_poll(void);
void ps2sif_usb_intr_stop(void);
static int handler(void *p) {
    assert(p==&ram); calls++;
    if(inject) { inject=false; ram.sequence++; }
    if(stop_in_handler) ps2sif_usb_intr_stop();
    return 1;
}
'''
code += section
code += r'''
static void reset(void) {
    usb_mailbox=NULL; usb_ready=usb_stopped=false; usb_ih=NULL;
    calls=removed=allocs=0; flag=enable=disable=0; exec_error=entry_result=0;
    usb_recovered=usb_sbus_count=0;
}
int main(void) {
    reset(); assert(ps2sif_usb_intr_establish(handler,&ram)==0);
    assert(allocs==1 && calls==0);
    ps2sif_usb_intr_poll(); /* startup pending before ready, no SBUS edge */
    assert(calls==1 && ram.acknowledged==1 && enable==0x80000000U);
    ps2sif_usb_intr_poll(); assert(calls==1); /* duplicate */
    ram.sequence++; flag=0x400; assert(usb_relay_intr(NULL)==1);
    assert(calls==2 && ram.acknowledged==2 && usb_sbus_count==1);
    flag=0; assert(usb_relay_intr(NULL)==0); assert(calls==2);
    ram.sequence++; ps2sif_usb_intr_poll(); /* lost edge */
    assert(calls==3 && ram.acknowledged==3);
    inject=true; ram.sequence++; ps2sif_usb_intr_poll();
    assert(ram.acknowledged==4 && ram.sequence==5);
    ps2sif_usb_intr_poll(); assert(ram.acknowledged==5 && calls==5);
    ps2sif_usb_intr_stop(); assert(removed==1 && usb_stopped);
    enable=0; ram.sequence++; flag=0x400;
    usb_relay_intr(NULL); ps2sif_usb_intr_poll();
    assert(calls==5 && enable==0 && allocs==1); /* no rearm/free */
    ps2sif_usb_intr_stop(); assert(removed==1);
    reset(); exec_error=ETIMEDOUT;
    assert(ps2sif_usb_intr_establish(handler,&ram)==ETIMEDOUT);
    assert(usb_stopped && removed==1 && allocs==1);
    assert(ps2sif_usb_intr_establish(handler,&ram)==EBUSY);
    reset(); entry_result=1;
    assert(ps2sif_usb_intr_establish(handler,&ram)==EPROTO);
    reset(); assert(ps2sif_usb_intr_establish(handler,&ram)==0);
    ram.guard[5]=0; ps2sif_usb_intr_poll();
    assert(usb_stopped && calls==0 && disable==0x80000000U);
    reset(); assert(ps2sif_usb_intr_establish(handler,&ram)==0);
    stop_in_handler=true; ps2sif_usb_intr_poll();
    assert(usb_stopped && calls==1 && enable==0 && removed==1);
    puts("EE USB IRQ handshake/duplicate/lost edge/rearm race/stop/timeout/guard passed");
}
'''
with tempfile.TemporaryDirectory(prefix='ps2-usb-irq-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'sys/arch/playstation2/iop'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

# Compile the actual resident IOP source with MMIO/imports mocked. A fixed
# uncached-address mapping keeps its wire-address parser unchanged on the host.
iop_code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "usb_irq.h"
static unsigned disable, flag, notifyreg, suspended, released;
static int fail_register, fail_enable;
#define IOP_REG(a) (*((a)==0xbf801614U ? &disable : (a)==0xbd000030U ? &flag : &notifyreg))
int irq_register(int irq,int mode,int (*handler)(void *),void *arg) {
    assert(irq==22 && mode==1 && handler && !arg && suspended);
    assert(disable==0x80000000U); return fail_register;
}
int irq_release(int irq) { assert(irq==22); released++; return 0; }
int irq_enable(int irq) { assert(irq==22 && suspended); return fail_enable; }
int irq_suspend(int *s) { assert(!suspended); *s=42; suspended=1; return 0; }
int irq_resume(int s) { assert(s==42 && suspended); suspended=0; return 0; }
'''
iop_code += (root / 'sys/arch/playstation2/iop/usb_irq.c').read_text()
iop_code += r'''
int main(void) {
    void *p=mmap((void *)0xa0010000U,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(p==(void *)0xa0010000U);
    struct ps2_usb_irq_mailbox *m=p;
    char text[9]="00010000"; char *args[]={"test",text};
    for(unsigned i=0;i<12;i++) m->guard[i]=PS2_USB_IRQ_GUARD;
    assert(module_start(1,args)==1);
    memcpy(text,"00000000",9); assert(module_start(2,args)==1);
    memcpy(text,"001ffff0",9); assert(module_start(2,args)==1);
    memcpy(text,"00010001",9); assert(module_start(2,args)==1);
    memcpy(text,"0001000g",9); assert(module_start(2,args)==1);
    memcpy(text,"00010000",9); fail_register=-1;
    assert(module_start(2,args)==1 && !suspended && !m->magic);
    fail_register=0; fail_enable=-1;
    assert(module_start(2,args)==1 && released==1 && !suspended && !m->magic);
    fail_enable=0; notifyreg=0x80;
    assert(module_start(2,args)==0 && !suspended);
    assert(m->magic==PS2_USB_IRQ_MAGIC && m->sequence==1 && !m->interrupts);
    assert(flag==0x400 && notifyreg==0x82);
    assert(usb_interrupt(NULL)==1);
    assert(m->sequence==2 && m->interrupts==1 && disable==0x80000000U);
    for(unsigned i=0;i<12;i++) assert(m->guard[i]==PS2_USB_IRQ_GUARD);
    assert(m->acknowledged==0); /* IOP never writes EE's acknowledgement. */
    munmap(p,4096);
}
'''
with tempfile.TemporaryDirectory(prefix='ps2-iop-usb-irq-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(iop_code)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-int-to-pointer-cast',
                    '-I' + str(root / 'sys/arch/playstation2/iop'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('IOP USB IRQ parser/registration failure/residency/mask/notification/guards passed')
