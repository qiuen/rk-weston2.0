#ifndef DRM_HDMI_SET_H_
#define DRM_HDMI_SET_H_
#ifdef  __cplusplus
extern "C" {
#endif
#include "stdlib.h"

typedef struct {
   int xres;
   int yres;
   int refresh;
   int interlaced;
   int reserved[3];
} HdmiInfo_t;

typedef struct {

   int numHdmiMode;
   HdmiInfo_t *hdmimode;
   
} Hdmi_Info_list_t;

int init_hdmi_set(int fd);

int deInit_hdmi_set();

int hdmi_set_resolution(int xres, int yres, int refresh, bool interlaced);

int UpdateDisplaySize(int src_xpos, int src_ypos, int src_w, int src_h, int dst_xpos, int dst_ypos, int dst_w, int dst_h);

int hdmi_get_resolution(Hdmi_Info_list_t *list);

int set_plane(int fb_id, int crtc_id);

int hdmi_check_mode(int xres, int yres, int refresh, int flag, int clock);

int hdmi_get_current_mode(int *xres, int *yres);

int hdmi_get_last_resolution(int* xres, int* yres, int* refresh, bool* interlaced, int type);

void set_best_hdmi_mode(int xres, int yres, int refresh, bool interlaced);

#ifdef  __cplusplus
  }
#endif




#endif




