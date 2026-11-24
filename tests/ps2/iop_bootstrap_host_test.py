#!/usr/bin/env python3
"""Check production RPC bootstrap restoration/lifetime and the actual 32-bit ABI."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
iop = root / "sys/arch/playstation2/iop"
sif = (root / "sys/arch/playstation2/ee/sif_native.c").read_text()
header = (root / "sys/arch/playstation2/ee/sif_native.h").read_text()
result = header[header.index("struct ps2sif_module_result {"):]
result = result[:result.index("};") + 2]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "bootstrap.h"
#include "ps2_iop_images.h"
#define IO_LIMIT 0x200000U
#define RPC_CALL 0x8000000aU
#define MIN(a,b) ((a)<(b)?(a):(b))
#define roundup(a,b) (((a)+(b)-1)&~((b)-1))
typedef int kmutex_t;
static kmutex_t rpc_lock;
static bool service_ready;
static uint32_t bound_server,bound_buffer,reply[32],ram[IO_LIMIT/4];
static unsigned allocated,freed,submitted;
static int fail_alloc,fail_bind,fail_upload,fail_send,fail_wait,fail_free;
static bool bad_signature,bad_module,bad_address;
static void mutex_enter(kmutex_t *m) { assert(!*m); *m=1; }
static void mutex_exit(kmutex_t *m) { assert(*m); *m=0; }
static volatile uint32_t *iop_window(uint32_t a) { assert(a<IO_LIMIT && !(a&3)); return &ram[a/4]; }
static void barrier(void) {}
static uint32_t physical(void *p) { assert(p==reply); return 0x400000; }
static int heap_call(unsigned op,uint32_t arg,uint32_t *out) {
 if(op==1) {
  if(fail_alloc) return fail_alloc;
  assert(arg>=64 && arg<65536+4096); allocated++;
  *out=bad_address?IO_LIMIT-16:0x10000; return 0;
 }
 assert(op==2 && arg==0x10000); freed++;
 if(fail_free) return fail_free;
 *out=0; return 0;
}
static int send_to_iop(uint32_t dest,const uint32_t *data,size_t n,bool cmd) {
 assert(!cmd && n && n<=128 && !(n&3) && dest<IO_LIMIT-n);
 if(fail_upload) return fail_upload;
 memcpy(&ram[dest/4],data,n); return 0;
}
static int bind_service(uint32_t sid) {
 assert(sid==0x80000006U);
 if(fail_bind) return fail_bind;
 bound_server=0x2000; bound_buffer=0x3000; return 0;
}
static void rpc_header(uint32_t *p,uint32_t cmd) { assert(cmd==RPC_CALL); p[4]=5; }
static int send_command(uint32_t *p,unsigned n,uint32_t cmd) {
 assert(n==64 && cmd==RPC_CALL); submitted++;
 assert(ram[0x2004/4]==(0xa0010000U+roundup(64,16)));
 assert(p[8]==PS2_IOP_BOOT_COMMAND && p[9]==64 && p[11]==16 && p[13]==0x2000);
 struct ps2_iop_boot_request *r=(void*)&ram[0x3000/4];
 assert(r->flush_icache==0x6000 && r->load_module==0x6200 && r->start_module==0x6100);
 assert(r->image==0x10000 && r->argument_size==9);
 assert(!memcmp(r->arguments,"00012340",9) && !memcmp(r->name,"NetBSD",7));
 if(fail_send) return fail_send;
 return 0;
}
static int wait_reply(void) {
 if(fail_wait) return fail_wait;
 reply[0]=bad_module?(uint32_t)-201:25;
 reply[1]=1; reply[2]=bad_module?(uint32_t)-1:25;
 reply[3]=bad_signature?0:PS2_IOP_BOOT_SIGNATURE;
 return 0;
}
'''
code += result + "\n"
code += sif[sif.index("static bool\niop_function("):sif.index("static void\nbootstrap_selftest(")]
code += r'''
static void reset(void) {
 memset(ram,0,sizeof(ram)); memset(reply,0,sizeof(reply));
 fail_alloc=fail_bind=fail_upload=fail_send=fail_wait=fail_free=0;
 bad_signature=bad_module=bad_address=false;
 rpc_lock=0; service_ready=true; bound_server=0x1500; bound_buffer=0x1600;
 allocated=freed=submitted=0;
 ram[0x2000/4]=0x80000006U; ram[0x2004/4]=0x4000;
 uint32_t *v=&ram[0x5000/4]; v[3]=0x64616f6cU; v[4]=0x65726f63U; v[9]=0x6000;
 v=&ram[0x5100/4]; v[3]=0x6c646f6dU; v[4]=0x0064616fU; v[13]=0x6100; v[15]=0x6200;
}
static void restored(void) {
 assert(!rpc_lock && bound_server==0x1500 && bound_buffer==0x1600);
 assert(ram[0x2004/4]==0x4000);
}
int main(void) {
 uint32_t image[16]={0x464c457fU}; struct ps2sif_module_result r;
 const char arg[]="00012340";
 #define EXEC() ps2sif_exec_irx(image,sizeof(image),arg,sizeof(arg),&r)
 reset(); assert(EXEC()==0); restored();
 assert(allocated==1 && freed==1 && submitted==1 && service_ready);
 assert(r.module_id==25 && r.entry_result==1 && r.signature==PS2_IOP_BOOT_SIGNATURE);
 assert(!memcmp(&ram[0x10000/4],image,sizeof(image)));
 assert(!memcmp(&ram[(0x10000+64)/4],ps2_iop_bootstrap_image,sizeof(ps2_iop_bootstrap_image)));
 assert(iop_function(0x1000) && iop_function(0x80001000) && iop_function(0xa0001000));
 assert(!iop_function(0x40001000) && !iop_function(0x1fffffff) && !iop_function(0));
 reset(); assert(ps2sif_exec_irx(image,0,arg,sizeof(arg),&r)==EINVAL); restored();
 assert(ps2sif_exec_irx((void*)((char*)image+1),64,arg,sizeof(arg),&r)==EINVAL);
 assert(ps2sif_exec_irx(image,64,"no-nul",6,&r)==EINVAL);
 reset(); service_ready=false; assert(EXEC()==EIO); restored(); assert(!allocated);
 reset(); ram[0x5024/4]=0; assert(EXEC()==ENOTSUP); restored(); assert(!allocated);
 reset(); memcpy(&ram[0x5200/4],&ram[0x5100/4],64);
 assert(EXEC()==ENOTSUP); restored(); assert(!allocated); /* ambiguous exports */
 reset(); fail_alloc=ENOMEM; assert(EXEC()==ENOMEM); restored(); assert(!freed);
 reset(); bad_address=true; assert(EXEC()==EPROTO); restored(); assert(!freed);
 reset(); fail_upload=EIO; assert(EXEC()==EIO); restored(); assert(freed==1 && !submitted);
 reset(); fail_bind=ENXIO; assert(EXEC()==ENXIO); restored(); assert(freed==1 && !submitted);
 reset(); fail_send=ETIMEDOUT; assert(EXEC()==ETIMEDOUT); restored();
 assert(!freed && !service_ready); /* delivery uncertain: retain executable data */
 reset(); fail_wait=ETIMEDOUT; assert(EXEC()==ETIMEDOUT); restored();
 assert(!freed && !service_ready);
 reset(); bad_signature=true; assert(EXEC()==EPROTO); restored(); assert(freed==1);
 reset(); bad_module=true; assert(EXEC()==ENOEXEC); restored(); assert(freed==1);
 reset(); fail_free=EIO; assert(EXEC()==EIO); restored(); assert(!service_ready);
 puts("IOP bootstrap bounds/export ambiguity/callback restoration/timeout lifetime checks passed");
}
'''
abi = r'''
#include "bootstrap.h"
extern void *ps2_iop_bootstrap(int,void*,int);
static int calls,fail_load;
static void flush(void) { calls=calls*10+1; }
static int load(void *p) { if(p!=(void*)0x12340) return -999; calls=calls*10+2; return fail_load?-201:25; }
static int start(int id,const char *n,int len,const char *a,int *result) {
 if(id!=25 || n[0]!='N' || len!=9 || a[0]!='0') return -999;
 calls=calls*10+3; *result=1; return 25;
}
static struct ps2_iop_boot_request r;
static void init(void) {
 r=(struct ps2_iop_boot_request){.flush_icache=(unsigned int)flush,
 .load_module=(unsigned int)load,.start_module=(unsigned int)start,
 .image=0x12340,.argument_size=9,.module_id=-1,.module_result=-1,.start_result=-1};
 r.name[0]='N'; r.arguments[0]='0'; calls=0;
}
static int test(void) {
 init(); if(ps2_iop_bootstrap(0,&r,64)!=&r || calls || r.signature) return 1;
 if(ps2_iop_bootstrap(PS2_IOP_BOOT_COMMAND,&r,63)!=&r || calls) return 2;
 r.argument_size=17;
 if(ps2_iop_bootstrap(PS2_IOP_BOOT_COMMAND,&r,64)!=&r || calls) return 3;
 init(); if(ps2_iop_bootstrap(PS2_IOP_BOOT_COMMAND,&r,64)!=&r.module_id) return 4;
 if(calls!=123 || r.stage!=4 || r.module_id!=25 || r.module_result!=1 ||
 r.start_result!=25 || r.signature!=PS2_IOP_BOOT_SIGNATURE) return 5;
 init(); fail_load=1; ps2_iop_bootstrap(PS2_IOP_BOOT_COMMAND,&r,64);
 if(calls!=12 || r.stage!=3 || r.module_id!=-201 || r.start_result!=-1) return 6;
 return 0;
}
void _start(void) {
 int status=test();
 __asm volatile("int $0x80"::"a"(1),"b"(status):"memory");
 __builtin_unreachable();
}
'''
with tempfile.TemporaryDirectory(prefix="ps2-iop-bootstrap-") as temp:
    d = Path(temp)
    (d / "rpc.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-const-variable", "-I", str(iop), str(d / "rpc.c"),
                    "-o", str(d / "rpc")], check=True)
    subprocess.run([str(d / "rpc")], check=True)
    (d / "abi.c").write_text(abi)
    subprocess.run(["cc", "-m32", "-nostdlib", "-static", "-no-pie", "-fno-pic",
                    "-fno-stack-protector", "-O2", "-ffreestanding", "-I", str(iop),
                    str(d / "abi.c"), str(iop / "bootstrap.c"),
                    "-o", str(d / "abi")], check=True)
    subprocess.run([str(d / "abi")], check=True)
    print("Actual 32-bit bootstrap ABI/callback ordering/load failure checks passed")
