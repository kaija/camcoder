#ifndef __SPS_H
#define __SPS_H
int iot_h264_decode_seq_parameter_set(unsigned char *spsBuf, unsigned int len, int *sdpWidth, int *sdpHeight);
#endif
