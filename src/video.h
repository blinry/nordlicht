#ifndef INCLUDE_video_h__
#define INCLUDE_video_h__

#include "common.h"

typedef struct video video;

video* video_init(char *filename);

column* video_get_column(video *f, double min_percent, double max_percent, nordlicht_style s);

void video_free(video *f);

#endif
