/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/mman.h>
//#include <cairo.h>

#include <wayland-client.h>
#include "weston-screenshooter-client-protocol.h"
#include "shared/os-compatibility.h"
#include "shared/xalloc.h"
#include "displayconfig.h"
/* The screenshooter is a good example of a custom object exposed by
 * the compositor and serves as a test bed for implementing client
 * side marshalling outside libwayland.so */

static struct wl_shm *shm;
static struct weston_screenshooter *screenshooter;
static struct weston_display_coinfig *displayconfig;
static struct wl_list output_list;
static struct wl_display *display;
static struct wl_registry *registry;
static HdmiInfos_t _HdmiInfos;
int min_x, min_y, max_x, max_y;
int buffer_copy_done;
int config_done;

struct screenshooter_output {
	struct wl_output *output;
	struct wl_buffer *buffer;
	int width, height, offset_x, offset_y;
    struct wl_buffer *displayconfig_buffer;
	void *data_displayconfig;
	char   modes[100];
	void *data;
	struct wl_list link;
};


static void
display_handle_geometry(void *data,
			struct wl_output *wl_output,
			int x,
			int y,
			int physical_width,
			int physical_height,
			int subpixel,
			const char *make,
			const char *model,
			int transform)
{
	struct screenshooter_output *output;
    printf("=============x=%d,y=%d,physical_width=%d,physical_height=%d,mode=%s\n",x, y, physical_width, physical_height,model);
	output = wl_output_get_user_data(wl_output);

	if (wl_output == output->output) {
		output->offset_x = x;
		output->offset_y = y;
	}
}

static void
display_handle_mode(void *data,
		    struct wl_output *wl_output,
		    uint32_t flags,
		    int width,
		    int height,
		    int refresh)
{
	struct screenshooter_output *output;

	output = wl_output_get_user_data(wl_output);

	if (wl_output == output->output && (flags & WL_OUTPUT_MODE_CURRENT)) {
		output->width = width;
		output->height = height;
		printf("+++++++display_handle_mode width=%d,height=%d\n",output->width,output->height);
	}
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
        /* don't bother waiting for this; there's no good reason a
         * compositor will wait more than one roundtrip before sending
         * these initial events. */
         printf("++++++output_handle_done\n");
}

static void
output_handle_scale(void *data, struct wl_output *wl_output,
                    int32_t scale)
{

		printf("++++++output_handle_scale\n");
}


static const struct wl_output_listener output_listener = {
	display_handle_geometry,
	display_handle_mode,
	output_handle_done,
    output_handle_scale,
};

static void
screenshot_done(void *data, struct weston_screenshooter *screenshooter)
{
	buffer_copy_done = 1;
}

static void
displayconfig_done(void *data, struct weston_display_coinfig *displayconfig) {
	config_done = 1;
}

static const struct weston_screenshooter_listener screenshooter_listener = {
	screenshot_done
};

static const struct weston_display_coinfig_listener displayconfig_listener = {
	displayconfig_done
};

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t name, const char *interface, uint32_t version)
{
	static struct screenshooter_output *output;
    printf("++++++interface=%s,name=%d\n",interface,name);
	if (strcmp(interface, "wl_output") == 0) {
		output = xmalloc(sizeof *output);
		output->output = wl_registry_bind(registry, name,
						  &wl_output_interface, 1);
		wl_list_insert(&output_list, &output->link);
		wl_output_add_listener(output->output, &output_listener, output);
	} else if (strcmp(interface, "wl_shm") == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, "weston_screenshooter") == 0) {
		screenshooter = wl_registry_bind(registry, name,
						 &weston_screenshooter_interface,
						 1);
	} else if (strcmp(interface, "weston_display_coinfig") == 0){
		displayconfig = wl_registry_bind(registry, name,
						 &weston_display_coinfig_interface,
						 1);
	}
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	/* XXX: unimplemented */
}

static const struct wl_registry_listener registry_listener = {
	handle_global,
	handle_global_remove
};

static struct wl_buffer *
create_shm_buffer(int width, int height, void **data_out)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	int fd, size, stride;
	void *data;

	stride = width * 4;
	size = stride * height;

	fd = os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
			size);
		return NULL;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	pool = wl_shm_create_pool(shm, fd, size);
	close(fd);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
					   WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);

	*data_out = data;

	return buffer;
}
/*
static void
write_png(int width, int height)
{
	int output_stride, buffer_stride, i;
	cairo_surface_t *surface;
	void *data, *d, *s;
	struct screenshooter_output *output, *next;

	buffer_stride = width * 4;

	data = xmalloc(buffer_stride * height);
	if (!data)
		return;

	wl_list_for_each_safe(output, next, &output_list, link) {
		output_stride = output->width * 4;
		s = output->data;
		d = data + (output->offset_y - min_y) * buffer_stride +
			   (output->offset_x - min_x) * 4;

		for (i = 0; i < output->height; i++) {
			memcpy(d, s, output_stride);
			d += buffer_stride;
			s += output_stride;
		}

		free(output);
	}

	surface = cairo_image_surface_create_for_data(data,
						      CAIRO_FORMAT_ARGB32,
						      width, height, buffer_stride);
	cairo_surface_write_to_png(surface, "wayland-screenshot.png");
	cairo_surface_destroy(surface);
	free(data);
}
*/
static int
set_buffer_size(int *width, int *height)
{
	struct screenshooter_output *output;
	min_x = min_y = INT_MAX;
	max_x = max_y = INT_MIN;
	int position = 0;

	wl_list_for_each_reverse(output, &output_list, link) {
		output->offset_x = position;
		position += output->width;
	}

	wl_list_for_each(output, &output_list, link) {
		min_x = MIN(min_x, output->offset_x);
		min_y = MIN(min_y, output->offset_y);
		max_x = MAX(max_x, output->offset_x + output->width);
		max_y = MAX(max_y, output->offset_y + output->height);
	}

	if (max_x <= min_x || max_y <= min_y)
		return -1;

	*width = max_x - min_x;
	*height = max_y - min_y;

	return 0;
}



WL_EXPORT  int 
init_display_config() {

	struct screenshooter_output *output;

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "=====failed to create display: %m\n");
		return -1;
	}

	wl_list_init(&output_list);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (displayconfig == NULL) {
		fprintf(stderr, "display doesn't support displayconfig\n");
		return -1;
	}

	/*weston_screenshooter_add_listener(screenshooter,
					  &screenshooter_listener,
					  screenshooter);*/
					  
	weston_display_coinfig_add_listener(displayconfig, 
					  &displayconfig_listener, 
					  displayconfig);

	/*if (set_buffer_size(&width, &height))
		return -1;*/
       
   printf("%s succ.\n",__FUNCTION__); 
   
   return 0;
}

WL_EXPORT int 
get_display_hdmi_info(HdmiInfos_t *pHdmiInfo) {
	
   struct screenshooter_output *output;
   wl_list_for_each(output, &output_list, link) {

		output->buffer = create_shm_buffer(100, 100, &output->data);
		weston_display_coinfig_GetResource2(displayconfig, output->output, output->buffer);		

        //sync client and server
		config_done = 0;
		while (!config_done)
		    wl_display_roundtrip(display);

		memcpy((void*)&_HdmiInfos, output->data, sizeof(HdmiInfos_t));
		
		if (pHdmiInfo != NULL) {
           memcpy(pHdmiInfo, (void*)&_HdmiInfos, sizeof(HdmiInfos_t));
		}
	}
   printf("%s succ.\n",__FUNCTION__); 
   return 0;
}

WL_EXPORT int 
set_hdmi_mode(int width, int height, int refresh, int interlaced, int reserved) {

	struct screenshooter_output *output;
	printf("%s enter.\n",__FUNCTION__); 
	wl_list_for_each(output, &output_list, link) {
		for (int i=0; i<_HdmiInfos.count; i++) {
			printf("i=%d, width=%d,height=%d,refresh=%d\n",i,_HdmiInfos.hdmi_info[i].xres,_HdmiInfos.hdmi_info[i].yres,_HdmiInfos.hdmi_info[i].refresh);
			if (_HdmiInfos.hdmi_info[i].xres==width && _HdmiInfos.hdmi_info[i].yres==height && _HdmiInfos.hdmi_info[i].refresh==refresh && _HdmiInfos.hdmi_info[i].interlaced==interlaced) {
				weston_display_coinfig_SetMode(displayconfig, output->output, _HdmiInfos.hdmi_info[i].xres, \
					                             _HdmiInfos.hdmi_info[i].yres, _HdmiInfos.hdmi_info[i].refresh, interlaced, 0);

				break;
			}
			
		}
	}
	config_done = 0;
	while (!config_done)
	wl_display_roundtrip(display);
    printf("%s succ.\n",__FUNCTION__); 
	return 0;
}

WL_EXPORT int deInit_display_config() {
   
   return 0;
}

#if 0
int main(int argc, char *argv[])
{
	struct wl_display *display;
	struct wl_registry *registry;
	struct screenshooter_output *output;
	int width, height;

/*	if (getenv("WAYLAND_SOCKET") == NULL) {
		fprintf(stderr, "%s must be launched by weston.\n"
			"Use the MOD+S shortcut to take a screenshot.\n",
			program_invocation_short_name);
		return -1;
	}*/
    fprintf(stderr,"++++++++++++++++++++\n");
	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	wl_list_init(&output_list);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (screenshooter == NULL) {
		fprintf(stderr, "display doesn't support screenshooter\n");
		return -1;
	}
	
	if (displayconfig == NULL) {
		fprintf(stderr, "display doesn't support displayconfig\n");
		return -1;
	}

	weston_screenshooter_add_listener(screenshooter,
					  &screenshooter_listener,
					  screenshooter);
					  
	weston_display_coinfig_add_listener(displayconfig, 
					  &displayconfig_listener, 
					  displayconfig);

	if (set_buffer_size(&width, &height))
		return -1;
    //char *testConfig = (char*) malloc(20);
    char testConfig[100];
	memset(testConfig, 0, 20);
	memset(testConfig, "111111111\0", 10);
    weston_display_coinfig_GetResource(displayconfig, "222222222");
	//weston_display_coinfig_GetResource2(displayconfig, output->output, );
    
	wl_list_for_each(output, &output_list, link) {
		output->buffer = create_shm_buffer(100, 100, &output->data);
		output->displayconfig_buffer = create_shm_buffer(output->width, output->height, &output->data_displayconfig);
		weston_screenshooter_shoot(screenshooter,
					   output->output,
					   output->buffer);
		
		memset((void*)output->data_displayconfig, 0, 100);
        weston_display_coinfig_GetResource2(displayconfig, output->output, output->displayconfig_buffer);
       
		
		config_done = 0;
        while (!config_done)
			wl_display_roundtrip(display);

		HdmiInfos_t hdmiinfo;
		memset(&hdmiinfo,   0 , sizeof(HdmiInfo_t));
    /*    printf("+++++++%x,%x,%x,%x,%x,%x,%x,%x\n",output->data_displayconfig[0],output->data_displayconfig[1],\
			                                      output->data_displayconfig[2],output->data_displayconfig[3],\
			                                      output->data_displayconfig[4],output->data_displayconfig[5],\
			                                      output->data_displayconfig[6],output->data_displayconfig[7]);*/

		memcpy((void*)&hdmiinfo, output->data_displayconfig, sizeof(HdmiInfos_t));
		
		//printf("=========hdmiinfo:w=%d,h=%d,refresh=%d\n", hdmiinfo.xres, hdmiinfo.yres, hdmiinfo.refresh);
        printf("===============count=%d\n",hdmiinfo.count);

		for (int i=0; i<hdmiinfo.count; i++) {
             printf("i=%d, width=%d,height=%d,refresh=%d\n",i,hdmiinfo.hdmi_info[i].xres,hdmiinfo.hdmi_info[i].yres,hdmiinfo.hdmi_info[i].refresh);
             if (hdmiinfo.hdmi_info[i].xres==1280 && hdmiinfo.hdmi_info[i].yres==720 && hdmiinfo.hdmi_info[i].refresh==60000) {
                weston_display_coinfig_SetMode(displayconfig, output->output, hdmiinfo.hdmi_info[i].xres, hdmiinfo.hdmi_info[i].yres, hdmiinfo.hdmi_info[i].refresh, 0, 0);
                break;
			 }
		}

		config_done = 0;
        while (!config_done)
			wl_display_roundtrip(display);
		//buffer_copy_done = 0;
		//while (!buffer_copy_done)
		//	wl_display_roundtrip(display);
	}

	//write_png(width, height);

	return 0;
}

#endif
