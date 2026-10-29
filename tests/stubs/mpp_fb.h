#pragma once
struct mpp_fb { unsigned unused; };
struct aicfb_layer_data { unsigned layer_id, enable, cookie; };
#define AICFB_LAYER_TYPE_UI 1
enum { AICFB_PAN_DISPLAY = 1, AICFB_WAIT_FOR_VSYNC, AICFB_GET_LAYER_CONFIG, AICFB_UPDATE_LAYER_CONFIG };
struct mpp_fb *mpp_fb_open(void);
void mpp_fb_close(struct mpp_fb *);
int mpp_fb_ioctl(struct mpp_fb *, int, void *);
