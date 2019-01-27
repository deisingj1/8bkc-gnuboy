
#include <stdlib.h>
#include <stdio.h>

#include "rom/ets_sys.h"
#include "fb.h"
#include "lcd.h"
#include <string.h>
#include "8bkc-hal.h"
#include "menu.h"

#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



static uint16_t *frontbuff=NULL, *backbuff=NULL;
static volatile uint16_t *toRender=NULL;
static volatile uint16_t *overlay=NULL;
struct fb fb;
static SemaphoreHandle_t renderSem;

static bool skipframe=false;
static bool doShutdown=false;

void vid_preinit()
{
}

void gnuboy_esp32_videohandler();
void lineTask();

void videoTask(void *pvparameters) {
	gnuboy_esp32_videohandler();
}


void vid_init()
{
	doShutdown=false;
	frontbuff=malloc(160*144*2);
	backbuff=malloc(160*144*2);
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 2;
	fb.pitch = 160*2;
	fb.ptr = (unsigned char*)frontbuff;
	fb.enabled = 1;
	fb.dirty = 0;

	fb.indexed = 0;
	fb.cc[0].r = fb.cc[2].r = 3;
	fb.cc[1].r = 2;
	fb.cc[0].l = 11;
	fb.cc[1].l = 5;
	fb.cc[2].l = 0;
	
	gbfemtoMenuInit();
	memset(frontbuff, 0, 160*144*2);
	memset(backbuff, 0, 160*144*2);

	renderSem=xSemaphoreCreateBinary();
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024*2, NULL, 5, NULL, 1);
	ets_printf("Video inited.\n");
}


void vid_close()
{
	doShutdown=true;
	xSemaphoreGive(renderSem);
	vTaskDelay(100); //wait till video thread shuts down... pretty dirty
	free(frontbuff);
	free(backbuff);
	vQueueDelete(renderSem);
}

void vid_settitle(char *title)
{
}

void vid_setpal(int i, int r, int g, int b)
{
}

extern int patcachemiss, patcachehit, frames;


void vid_begin()
{
//	vram_dirty(); //use this to find out a viable size for patcache
	frames++;
	patcachemiss=0;
	patcachehit=0;
	esp_task_wdt_feed();
}

void vid_end()
{
	overlay=NULL;
	toRender=(uint16_t*)fb.ptr;
	xSemaphoreGive(renderSem);
	if (fb.ptr == (unsigned char*)frontbuff ) {
		fb.ptr = (unsigned char*)backbuff;
	} else {
		fb.ptr = (unsigned char*)frontbuff;
	}
//	printf("Pcm %d pch %d\n", patcachemiss, patcachehit);
}

uint32_t *vidGetOverlayBuf() {
	return (uint32_t*)fb.ptr;
}

void vidRenderOverlay() {
	overlay=(uint16_t*)fb.ptr;
	if (fb.ptr == (unsigned char*)frontbuff ) toRender=(uint16_t*)backbuff; else toRender=(uint16_t*)frontbuff;
	xSemaphoreGive(renderSem);
}

void kb_init()
{
}

void kb_close()
{
}

void kb_poll()
{
}

void ev_poll()
{
	kb_poll();
}

int addOverlayPixel(uint16_t p, uint32_t ov) {
	int or, og, ob, a;
	int br, bg, bb;
	int r,g,b;
	br=((p>>11)&0x1f)<<3;
	bg=((p>>5)&0x3f)<<2;
	bb=((p>>0)&0x1f)<<3;

	a=(ov>>24)&0xff;
	//hack: Always show background darker
	a=(a/2)+128;

	ob=(ov>>16)&0xff;
	og=(ov>>8)&0xff;
	or=(ov>>0)&0xff;

	r=(br*(256-a))+(or*a);
	g=(bg*(256-a))+(og*a);
	b=(bb*(256-a))+(ob*a);
	int ret = ((r>>(3+8))<<11)+((g>>(2+8))<<5)+((b>>(3+8))<<0);
	return (ret>>8)+((ret&0xff)<<8);
}

//This thread runs on core 1.
void gnuboy_esp32_videohandler() {
	//uint16_t oledfb[160*144];
	uint16_t *oledfbptr;
	uint16_t c;
	uint32_t *ovl;
	volatile uint16_t *rendering;
	while(!doShutdown) {
		int ry; //Y on screen
		//if (toRender==NULL) 
		xSemaphoreTake(renderSem, portMAX_DELAY);
		rendering=toRender;
		ovl=(uint32_t*)overlay;
		if(ovl) {
			for(int y=0; y < 144; y++) {
				for(int x=0; x < 160;x++) {
					c=*rendering;
					if(y < 64 && x < 80) { 
						c=addOverlayPixel(c, *ovl++);
					}
					*rendering++=c;
				}
			}
		}
		kchal_send_fb(toRender);
	}
	vTaskDelete(NULL);
}





