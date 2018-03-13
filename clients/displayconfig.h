#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H


#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   int xres;
   int yres;
   int refresh;
   int interlaced;
   int reserved[3];
} HdmiInfo_t;

typedef struct {
   int count;
   HdmiInfo_t hdmi_info[100];
} HdmiInfos_t;


 int init_display_config();

 int get_display_hdmi_info(HdmiInfos_t *pHdmiInfo);

 int set_hdmi_mode(int width, int height, int refresh, int interlaced, int reserved);

 int deInit_display_config();





#endif








