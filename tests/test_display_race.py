"""Compile actual adapter ISR/list functions and inject the observed race window.
No hardware scheduling or PPA is mocked as proof of device recovery; this tests
only the lost-completion defect, on original AND patched sources.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]

def function(text,name):
    # Skip prototypes and select the definition.
    import re
    match=re.search(r'^[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',text,re.M)
    assert match,name
    start=match.start(); at=match.end(); depth=1
    while depth:
        if text[at]=='{': depth+=1
        elif text[at]=='}': depth-=1
        at+=1
    body=text[start:at]
    if body.startswith(name+"("): body="static bool " + body
    return body

preamble=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>
#define IRAM_ATTR
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define pdFALSE 0
#define pdTRUE 1
#define ESP_LV_ADAPTER_DUMMY_DRAW_EVT_FRAME_DONE 1
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL 3
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL 4
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL 5
typedef int BaseType_t;
typedef int TaskHandle_t;
typedef int esp_lv_adapter_tear_avoid_mode_t;
struct display_pipeline_buf {void *buffer; STAILQ_ENTRY(display_pipeline_buf) entry;};
STAILQ_HEAD(buf_list,display_pipeline_buf);
typedef struct {int lock; int elem_count; struct display_pipeline_buf *elems; struct buf_list busy_list,empty_list;} esp_lv_adapter_display_pipeline_t;
typedef struct {struct {struct {int tear_avoid_mode;} base;} cfg; bool rotated,gated,frame_buf_submit_pending; int double_sync_lock,double_wait_sem;void *pending_fb,*disp_fb; unsigned switch_seq;esp_lv_adapter_display_pipeline_t pipeline;} esp_lv_adapter_display_bridge_v9_t;
static unsigned notifications;
static bool bridge_mode_needs_submit_gated_frame_complete(esp_lv_adapter_display_bridge_v9_t*i){return i->gated;}
static bool bridge_mode_uses_rotated_nonpartial_pipeline_release(esp_lv_adapter_display_bridge_v9_t*i){return i->rotated;}
static bool bridge_mode_uses_buf_switch_release(int m){(void)m;return true;}
static void display_bridge_v9_signal_dummy_draw_event(esp_lv_adapter_display_bridge_v9_t*i,int e,int*y){(void)i;(void)e;(void)y;}
static TaskHandle_t display_bridge_v9_get_notify_task(esp_lv_adapter_display_bridge_v9_t*i){(void)i;return 1;}
static void vTaskNotifyGiveFromISR(int t,int*y){(void)t;(void)y;notifications++;}
static bool display_bridge_v9_complete_double_wait_isr(esp_lv_adapter_display_bridge_v9_t*i){(void)i;return true;}
static void xSemaphoreGiveFromISR(int s,int*y){(void)s;(void)y;}
'''
harness=r'''
int main(void) {
 for(unsigned schedule=0;schedule<4;schedule++) {
  struct display_pipeline_buf elem={.buffer=(void*)1};
  esp_lv_adapter_display_bridge_v9_t i={.rotated=true,.gated=true,.cfg.base.tear_avoid_mode=2};
  i.pipeline.elems=&elem;i.pipeline.elem_count=1;STAILQ_INIT(&i.pipeline.busy_list);STAILQ_INIT(&i.pipeline.empty_list);
  notifications=0;
  /* IRQ before draw returns, before flag publication. */
  if(schedule==0) display_bridge_v9_handle_frame_buf_complete(&i);
  display_bridge_v9_mark_frame_buf_submit(&i);
  /* Critical interleaving: IRQ after submit flag, before retired-buffer enqueue. */
  if(schedule==1 || schedule==3) display_bridge_v9_handle_frame_buf_complete(&i);
  display_bridge_pipeline_mark_buf_busy(&i.pipeline,elem.buffer);
  if(schedule==3) display_bridge_v9_handle_frame_buf_complete(&i);
  for(unsigned frame=0;frame<10;frame++) display_bridge_v9_handle_frame_buf_complete(&i);
  struct display_pipeline_buf *next=display_bridge_pipeline_take_free_buf(&i.pipeline);
  if(!EXPECT_FIXED && (schedule==1 || schedule==3)) {
   assert(next==NULL); assert(!i.frame_buf_submit_pending); assert(!STAILQ_EMPTY(&i.pipeline.busy_list));
  } else {
   assert(next==&elem);assert(STAILQ_EMPTY(&i.pipeline.busy_list));assert(!i.frame_buf_submit_pending);assert(notifications==1);
   assert(display_bridge_pipeline_take_free_buf(&i.pipeline)==NULL);
  }
 }
 puts(EXPECT_FIXED?"PASS: patched adapter returns buffer exactly once for every IRQ interleaving":"REPRODUCED: original adapter loses completion and strands buffer indefinitely");
}
'''
manifest=(ROOT/'firmware/main/idf_component.yml').read_text()
assert "override_path: '../vendor/esp_lvgl_adapter'" in manifest
for fixed in [False,True]:
    base=ROOT/'firmware/vendor/esp_lvgl_adapter'
    common=(base/'src/display/bridge/common/display_bridge_common.c').read_text()
    bridge=((base/'src/display/bridge/v9/lvgl_bridge_v9.c') if fixed else (ROOT/'tests/fixtures/adapter_064_completion.c')).read_text()
    source=preamble
    for name in ['display_bridge_pipeline_mark_buf_busy','display_bridge_pipeline_release_buf_isr','display_bridge_pipeline_take_free_buf']:
        source+='\n'+function(common,name)
    for name in ['display_bridge_v9_mark_frame_buf_submit','display_bridge_v9_consume_frame_buf_submit_isr','display_bridge_v9_handle_frame_buf_complete']:
        source+='\n'+function(bridge,name)
    with tempfile.TemporaryDirectory() as d:
        c=Path(d)/'race.c';c.write_text(source+'\n'+harness)
        exe=Path(d)/'race'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',f'-DEXPECT_FIXED={int(fixed)}',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
