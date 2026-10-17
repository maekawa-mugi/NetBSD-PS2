#!/usr/bin/env python3
"""Exercise production IOP DMA mapping and boot-HID decoding without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
dma = (root / "sys/arch/playstation2/ee/iopdma_native.c").read_text()
kbd = (root / "sys/arch/playstation2/dev/ps2kbd.c").read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define mstohz(n) (n)
typedef struct { bool owned; } kmutex_t;
typedef struct { bool armed; unsigned due; void (*fn)(void *); void *arg; } callout_t;
static unsigned now, closes, barriers;
static void mutex_enter(kmutex_t *m) { assert(!m->owned); m->owned=true; }
static void mutex_exit(kmutex_t *m) { assert(m->owned); m->owned=false; }
static void mutex_destroy(kmutex_t *m) { assert(!m->owned); }
static void callout_schedule(callout_t *c, unsigned t) { c->armed=true; c->due=now+t; }
static void callout_stop(callout_t *c) { c->armed=false; }
static void callout_halt(callout_t *c, void *p) { (void)p; c->armed=false; barriers++; }
static void callout_destroy(callout_t *c) { assert(!c->armed); }
static void advance(callout_t *c, unsigned t) {
    unsigned end=now+t;
    while(c->armed && c->due<=end) {
        now=c->due; c->armed=false; c->fn(c->arg);
    }
    now=end;
}
typedef void *device_t;
enum devact { DVACT_DEACTIVATE, OTHER_ACT };
static void *device_private(device_t d) { return d; }
static void uhidev_close(void *p) { (void)p; closes++; }
static void pmf_device_deregister(device_t d) { (void)d; }
#define IOP_WINDOW ((uintptr_t)0xbc000000)
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef void *bus_dma_tag_t;
struct proc { int dummy; };
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; uintptr_t _ds_vaddr; } bus_dma_segment_t;
struct testmap {
    size_t _dm_size; int _dm_segcnt; size_t _dm_boundary, dm_maxsegsz;
    int dm_nsegs; size_t dm_mapsize; bus_dma_segment_t dm_segs[8];
};
typedef struct testmap *bus_dmamap_t;
static bus_addr_t pool_base=0x45000;
static bus_size_t pool_size=512*1024;
static unsigned output_count;
static int output[128];
static void ps2_console_input(int c) { assert(output_count<128); output[output_count++]=c; }
'''
code += kbd[kbd.index("struct ps2kbd {"):kbd.index("static int\nascii_key(")]
code += dma[dma.index("static bool\nin_pool("):dma.index("static int\niop_alloc(")]
code += dma[dma.index("static int\niop_load("):dma.index("static int\niop_mbuf(")]
code += kbd[kbd.index("static int\nascii_key("):kbd.index("static int\nkeyboard_match(")]
code += kbd[kbd.index("static int\nkeyboard_activate("):kbd.index("CFATTACH_DECL_NEW")]
code += r'''
int main(void) {
    struct testmap m={._dm_size=65536,._dm_segcnt=8, .dm_maxsegsz=4096, ._dm_boundary=4096};
    uintptr_t va=IOP_WINDOW+pool_base;
    assert(iop_load(NULL,&m,(void*)va,8192,NULL,0)==0);
    assert(m.dm_nsegs==2 && m.dm_mapsize==8192);
    assert(m.dm_segs[0].ds_addr==pool_base && m.dm_segs[0].ds_len==4096);
    assert(m.dm_segs[1].ds_addr==pool_base+4096);
    assert(m.dm_segs[1]._ds_vaddr==va+4096);
    assert(iop_load(NULL,&m,(void*)(va+4000),200,NULL,0)==0);
    assert(m.dm_nsegs==2 && m.dm_segs[0].ds_len==96 && m.dm_segs[1].ds_len==104);
    assert(iop_load(NULL,&m,(void*)0x80100000,16,NULL,0)==EINVAL);
    assert(iop_load(NULL,&m,(void*)(va+pool_size-8),16,NULL,0)==EINVAL);
    assert(iop_load(NULL,&m,(void*)va,65537,NULL,0)==EINVAL);
    assert(iop_load(NULL,&m,(void*)va,0,NULL,0)==EINVAL);
    struct proc p;
    assert(iop_load(NULL,&m,(void*)va,16,&p,0)==EINVAL);
    m._dm_segcnt=1;
    assert(iop_load(NULL,&m,(void*)va,8192,NULL,0)==EFBIG);
    assert(m.dm_nsegs==0 && m.dm_mapsize==0);
    m._dm_segcnt=8; m._dm_boundary=0; m.dm_maxsegsz=0;
    assert(iop_load(NULL,&m,(void*)va,16,NULL,0)==EINVAL);
    assert(!in_pool(pool_base-1,1) && !in_pool(pool_base+pool_size,1));
    assert(ascii_key(4,0,false)=='a' && ascii_key(4,2,false)=='A');
    assert(ascii_key(4,0,true)=='A' && ascii_key(4,2,true)=='a');
    assert(ascii_key(6,1,false)==3 && ascii_key(7,0x10,false)==4);
    assert(ascii_key(30,0,false)=='1' && ascii_key(30,2,false)=='!');
    assert(ascii_key(40,0,false)=='\r' && ascii_key(42,0,false)=='\b');
    assert(ascii_key(43,0,false)=='\t' && ascii_key(76,0,false)==127);
    assert(ascii_key(255,0,false)==-1);
    struct ps2kbd s={0}; uint8_t r[8]={0,0,4,0,0,0,0,0};
    s.repeat.fn=keyboard_repeat; s.repeat.arg=&s;
    keyboard_report(&s,r,7); assert(output_count==0);
    keyboard_report(&s,r,8); assert(output_count==1 && output[0]=='a');
    keyboard_report(&s,r,8); assert(output_count==1); /* held key */
    r[2]=1; keyboard_report(&s,r,8); assert(output_count==1); /* rollover */
    memset(r,0,8); keyboard_report(&s,r,8);
    r[2]=57; keyboard_report(&s,r,8); assert(s.caps);
    keyboard_report(&s,r,8); assert(s.caps); /* no repeat toggle */
    memset(r,0,8); keyboard_report(&s,r,8);
    r[2]=4; r[3]=4; keyboard_report(&s,r,8);
    assert(output_count==2 && output[1]=='A'); /* duplicate slots */
    memset(r,0,8); keyboard_report(&s,r,8);
    r[0]=1; r[2]=6; keyboard_report(&s,r,8);
    assert(output_count==3 && output[2]==3);
    /* JIS punctuation: verify both ordinary and shifted key positions. */
    assert(ascii_key(31,2,false,true)=='"');
    assert(ascii_key(39,2,false,true)=='0');
    assert(ascii_key(45,2,false,true)=='=');
    assert(ascii_key(46,0,false,true)=='^' && ascii_key(46,2,false,true)=='~');
    assert(ascii_key(47,0,false,true)=='@' && ascii_key(47,2,false,true)=='`');
    assert(ascii_key(48,0,false,true)=='[' && ascii_key(48,2,false,true)=='{');
    assert(ascii_key(49,0,false,true)==']' && ascii_key(50,2,false,true)=='}');
    assert(ascii_key(51,2,false,true)=='+');
    assert(ascii_key(52,0,false,true)==':' && ascii_key(52,2,false,true)=='*');
    assert(ascii_key(135,2,false,true)=='_' && ascii_key(137,2,false,true)=='|');
    assert(ascii_key(137,0,false,true)==92);
    assert(ascii_key(53,0,false,true)==-1 && ascii_key(138,0,false,true)==-1);
    assert(ascii_key(47,1,false,true)==0 && ascii_key(46,1,false,true)==30);
    assert(ascii_key(48,1,false,true)==27 && ascii_key(50,1,false,true)==29);
    assert(ascii_key(135,3,false,true)==31 && ascii_key(137,1,false,true)==28);
    /* Hold: 500 ms initial delay, 33 ms repeats, current modifiers. */
    memset(r,0,8); keyboard_report(&s,r,8); s.caps=false;
    unsigned n=output_count;
    r[2]=4; keyboard_report(&s,r,8);
    advance(&s.repeat,499); assert(output_count==n+1);
    advance(&s.repeat,1); assert(output_count==n+2 && output[n+1]=='a');
    r[0]=2; keyboard_report(&s,r,8);
    advance(&s.repeat,33); assert(output_count==n+3 && output[n+2]=='A');
    memset(r,0,8); keyboard_report(&s,r,8);
    advance(&s.repeat,1000); assert(output_count==n+3 && !s.repeat.armed);
    /* New repeatable key wins; releasing it never revives an older key. */
    r[2]=4; keyboard_report(&s,r,8); r[3]=5; keyboard_report(&s,r,8);
    assert(s.repeat_key==5);
    advance(&s.repeat,500); assert(output[output_count-1]=='b');
    r[3]=0; keyboard_report(&s,r,8); assert(!s.repeat.armed);
    memset(r,0,8); keyboard_report(&s,r,8);
    r[2]=57; keyboard_report(&s,r,8); assert(!s.repeat.armed);
    memset(r,0,8); keyboard_report(&s,r,8);
    r[2]=4; keyboard_report(&s,r,8); r[2]=1; keyboard_report(&s,r,8);
    assert(!s.repeat.armed); /* no stuck repeat on rollover */
    memset(r,0,8); keyboard_report(&s,r,8);
    r[2]=4; keyboard_report(&s,r,8); n=output_count;
    assert(keyboard_activate(&s,OTHER_ACT)==EOPNOTSUPP);
    assert(keyboard_activate(&s,DVACT_DEACTIVATE)==0);
    advance(&s.repeat,1000); keyboard_report(&s,r,8);
    keyboard_repeat(&s); assert(output_count==n && !s.repeat.armed);
    s.opened=true; assert(keyboard_detach(&s,0)==0);
    assert(closes==1 && barriers==1 && !s.opened);
    /* A fresh attach has no held keys, repeat or Caps state from unplug. */
    struct ps2kbd replacement={0};
    replacement.repeat.fn=keyboard_repeat; replacement.repeat.arg=&replacement;
    keyboard_report(&replacement,r,8);
    assert(output[output_count-1]=='a');
    assert(keyboard_detach(&replacement,0)==0 && closes==1 && barriers==2);
    puts("Native IOP DMA and HID US/JIS/Ctrl/repeat/release/rollover/detach checks passed");
}
'''
# Existing three-argument assertions exercise the US map.
import re
code = re.sub(r'ascii_key\((\d+),(0x[0-9a-f]+|\d+),(false|true)\)',
              r'ascii_key(\1,\2,\3,false)', code)
# Raw C text above needs C character escapes, not a doubled backslash.
code = code.replace("'\\\\r'", "'\\r'").replace("'\\\\b'", "'\\b'").replace("'\\\\t'", "'\\t'")
with tempfile.TemporaryDirectory(prefix="ps2-usb-test-") as tmp:
    c = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c.write_text(code)
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra",
                    "-Wno-unused-parameter", "-Werror", str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
