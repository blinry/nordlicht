#ifndef INCLUDE_ffmpeg_h__
#define INCLUDE_ffmpeg_h__

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

typedef struct {
    AVFormatContext *format_context;
    AVCodecContext *decoder_context;
    int video_stream;
    AVFrame *frame;
} ffmpeg;

typedef struct {
    int width, height;
    unsigned char *data;
} image;

ffmpeg* ffmpeg_init(char *filename) {
    av_log_set_level(AV_LOG_QUIET);
    av_register_all();

    ffmpeg *f;
    f = malloc(sizeof(ffmpeg));
    f->frame = avcodec_alloc_frame();

    if (avformat_open_input(&f->format_context, filename, NULL, NULL) != 0)
        return NULL;
    if (avformat_find_stream_info(f->format_context, NULL) < 0)
        return NULL;
    f->video_stream = av_find_best_stream(f->format_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (f->video_stream == -1)
        return NULL;
    f->decoder_context = f->format_context->streams[f->video_stream]->codec;
    AVCodec *codec = NULL;
    codec = avcodec_find_decoder(f->decoder_context->codec_id);
    if (codec == NULL) {
        //die("Unsupported codec!");
        return NULL;
    }
    AVDictionary *optionsDict = NULL;
    if (avcodec_open2(f->decoder_context, codec, &optionsDict) < 0)
        return NULL;

    return f;
}

double fps(ffmpeg *f) {
    return av_q2d(f->format_context->streams[f->video_stream]->r_frame_rate);
}

double duration_sec(ffmpeg *f) {
    return (double)f->format_context->duration / (double)AV_TIME_BASE;
}

int total_number_of_frames(ffmpeg *f) {
    return fps(f)*duration_sec(f);
}

double grab_next_frame(ffmpeg *f) {
    int valid = 0;
    int got_frame = 0;

    AVPacket packet;

    double pts;

    while (!valid) {
        av_read_frame(f->format_context, &packet);
        avcodec_decode_video2(f->decoder_context, f->frame, &got_frame, &packet);
        if (got_frame) {
            pts = packet.pts;
            // to sec:
            pts = pts*(double)av_q2d(f->format_context->streams[f->video_stream]->time_base);
            // to frame number:
            pts = pts*fps(f);
            valid = 1;
        }
        av_free_packet(&packet);
    }

    return pts;
}

void seek(ffmpeg *f, long frame_nr) {
    //printf("seek: %d\n", frame_nr);
    double sec = frame_nr/fps(f);
    double time_stamp = f->format_context->streams[f->video_stream]->start_time;
    double time_base = av_q2d(f->format_context->streams[f->video_stream]->time_base);
    time_stamp += sec/time_base + 0.5;
    av_seek_frame(f->format_context, f->video_stream, time_stamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(f->format_context->streams[f->video_stream]->codec);

    long grabbed_frame_nr = -1;
    while (grabbed_frame_nr < frame_nr) {
        grabbed_frame_nr = grab_next_frame(f);
        //printf("frame: %d\n", grabbed_frame_nr);
        return 0;
    }
}

image* ffmpeg_get_frame(ffmpeg *f, double min_percent, double max_percent) {
    /*
    image *i;
    i = malloc(sizeof(image));
    i->width = 200;
    i->height = 300;
    i->data = malloc(i->height*i->width*3);
    int x,y;
    for (x=0; x<i->width; x++) {
        for (y=0; y<i->height; y++) {
            i->data[3*y*i->width+3*x] = y;
        }
    }
    */
    printf("%: %f\n", min_percent);
    seek(f, 1.0*total_number_of_frames(f)*min_percent);

    image *i;
    i = malloc(sizeof(image));

    i->width = f->frame->width;
    i->height = f->frame->height;
    i->data = malloc(i->height*i->width*3);

    ////////
    AVFrame *frame;
    frame = avcodec_alloc_frame();
    frame->width = i->width;
    frame->height = i->height;
    frame->format = PIX_FMT_RGB24;

    uint8_t *buffer;
    buffer = (uint8_t *)av_malloc(sizeof(uint8_t)*avpicture_get_size(PIX_FMT_RGB24, i->width, i->height));
    avpicture_fill((AVPicture *)frame, buffer, PIX_FMT_RGB24, i->width, i->height);
    //memset(frame->data[0], fill_color, frame->linesize[0]*i->height);

    struct SwsContext *sws_context = sws_getContext(f->frame->width, f->frame->height, f->frame->format,
            f->frame->width, f->frame->height, PIX_FMT_RGB24, SWS_AREA, NULL, NULL, NULL);
    sws_scale(sws_context, (uint8_t const * const *)f->frame->data,
            f->frame->linesize, 0, f->frame->height, frame->data,
            frame->linesize);
    sws_freeContext(sws_context);
    ///////

    int y;
    for (y=0; y<i->height; y++) {
        memcpy(i->data+y*i->width*3, frame->data[0]+y*frame->linesize[0], frame->linesize[0]);
    }

    return i;
}

unsigned char* compress_to_column(image *i) {
    unsigned char *column;
    column = malloc(i->height*3);

    int x, y;
    for (y=0; y<i->height; y++) {
        long rsum = 0;
        long gsum = 0;
        long bsum = 0;
        for (x=0; x<i->width; x++) {
            bsum += i->data[y*i->width*3+3*x+0];
            gsum += i->data[y*i->width*3+3*x+1];
            rsum += i->data[y*i->width*3+3*x+2];
        }
        column[3*y+0] = 1.0*rsum/i->width;
        column[3*y+1] = 1.0*gsum/i->width;
        column[3*y+2] = 1.0*bsum/i->width;
        /*
        column[3*y+0] = i->data[3*y*i->width+3*200+2];
        column[3*y+1] = i->data[3*y*i->width+3*200+1];
        column[3*y+2] = i->data[3*y*i->width+3*200+0];
        */
    }
    //memset(column, 0, n->height*3);
    //memcpy(column, i->data, n->height*3);

    return column;
}

unsigned char* ffmpeg_get_column(ffmpeg *f, double min_percent, double max_percent) {
    /*
    char *column;
    column = malloc(300*3);
    memset(column, 255*min_percent, 300*3);
    return column;
    */


    image *i = ffmpeg_get_frame(f, min_percent, max_percent);
    return compress_to_column(i);
}

#endif
