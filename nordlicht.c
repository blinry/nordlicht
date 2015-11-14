#include "nordlicht.h"
#include <string.h>
#include "error.h"
#include "source.h"

#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#endif

typedef struct {
    nordlicht_style style;
    int height;
} track;

struct nordlicht {
    int width, height;
    const char *filename;
    track *tracks;
    int num_tracks;
    unsigned char *data;

    int owns_data;
    int modifiable;
    nordlicht_strategy strategy;
    float progress;
    source *source;
};

size_t nordlicht_buffer_size(const nordlicht *n) {
    return n->width * n->height * 4;
}

nordlicht* nordlicht_init(const char *filename, const int width, const int height) {
    if (width < 1 || height < 1) {
        error("Dimensions must be positive (got %dx%d)", width, height);
        return NULL;
    }
    nordlicht *n;
    n = (nordlicht *) malloc(sizeof(nordlicht));

    n->width = width;
    n->height = height;
    n->filename = filename;

    n->data = (unsigned char *) calloc(nordlicht_buffer_size(n), 1);
    if (n->data == 0) {
        error("Not enough memory to allocate %d bytes", nordlicht_buffer_size(n));
        return NULL;
    }

    n->owns_data = 1;

    n->num_tracks = 1;
    n->tracks = (track *) malloc(sizeof(track));
    n->tracks[0].style = NORDLICHT_STYLE_HORIZONTAL;
    n->tracks[0].height = n->height;

    n->strategy = NORDLICHT_STRATEGY_FAST;
    n->modifiable = 1;
    n->progress = 0;
    n->source = source_init(filename);

    if (n->source == NULL) {
        error("Could not open video file '%s'", filename);
        free(n);
        return NULL;
    }

    return n;
}

void nordlicht_free(nordlicht *n) {
    if (n->owns_data) {
        free(n->data);
    }
    free(n->tracks);
    source_free(n->source);
    free(n);
}

const char *nordlicht_error() {
    return get_error();
}

int nordlicht_set_start(nordlicht *n, const float start) {
    if (! n->modifiable) {
        return -1;
    }

    if (start < 0) {
        error("'start' has to be >= 0.");
        return -1;
    }

    if (start >= source_end(n->source)) {
        error("'start' has to be smaller than 'end'.");
        return -1;
    }

    source_set_start(n->source, start);
    return 0;
}

int nordlicht_set_end(nordlicht *n, const float end) {
    if (! n->modifiable) {
        return -1;
    }

    if (end > 1) {
        error("'end' has to be <= 1.");
        return -1;
    }

    if (source_start(n->source) >= end) {
        error("'start' has to be smaller than 'end'.");
        return -1;
    }

    source_set_end(n->source, end);
    return 0;
}

int nordlicht_set_style(nordlicht *n, const nordlicht_style *styles, const int num_tracks) {
    if (! n->modifiable) {
        return -1;
    }

    n->num_tracks = num_tracks;

    if (n->num_tracks > n->height) {
        error("Height of %d px is too low for %d styles", n->height, n->num_tracks);
        return -1;
    }

    free(n->tracks);
    n->tracks = (track *) malloc(n->num_tracks*sizeof(track));

    int height_of_each_track = n->height/n->num_tracks;
    int i;
    for (i=0; i<num_tracks; i++) {
        nordlicht_style s = styles[i];
        if (s > NORDLICHT_STYLE_LAST-1) {
            return -1;
        }

        n->tracks[i].style = s;
        n->tracks[i].height = height_of_each_track;
    }
    n->tracks[0].height = n->height - (n->num_tracks-1)*height_of_each_track;

    return 0;
}

int nordlicht_set_strategy(nordlicht *n, const nordlicht_strategy s) {
    if (! n->modifiable) {
        return -1;
    }
    if (s > NORDLICHT_STRATEGY_LIVE) {
        return -1;
    }
    n->strategy = s;
    return 0;
}

int nordlicht_generate(nordlicht *n) {
    n->modifiable = 0;

    source_build_keyframe_index(n->source, n->width);

    int x, exact;

    const int do_a_fast_pass = (n->strategy == NORDLICHT_STRATEGY_LIVE) || !source_exact(n->source);
    const int do_an_exact_pass = source_exact(n->source);

    for (exact = (!do_a_fast_pass); exact <= do_an_exact_pass; exact++) {
        int i;
        int y_offset = 0;
        for(i = 0; i < n->num_tracks; i++) {
            // call this for each track, to seek to the beginning
            source_set_exact(n->source, exact);

            for (x = 0; x < n->width; x++) {
                image *frame;

                if (n->tracks[i].style == NORDLICHT_STYLE_SPECTROGRAM) {
                    if (!source_has_audio(n->source)) {
                        error("File contains no audio, please select an appropriate style");
                        n->progress = 1;
                        return -1;
                    }
                    frame = source_get_audio_frame(n->source, 1.0*(x+0.5-COLUMN_PRECISION/2.0)/n->width,
                            1.0*(x+0.5+COLUMN_PRECISION/2.0)/n->width);
                } else {
                    if (!source_has_video(n->source)) {
                        error("File contains no video, please select an appropriate style");
                        n->progress = 1;
                        return -1;
                    }
                    frame = source_get_video_frame(n->source, 1.0*(x+0.5-COLUMN_PRECISION/2.0)/n->width,
                            1.0*(x+0.5+COLUMN_PRECISION/2.0)/n->width);
                }
                if (frame == NULL) {
                    continue;
                }

                int thumbnail_width = 1.0*(image_width(frame)*n->tracks[i].height/image_height(frame));
                image *column = NULL;
                image *tmp = NULL;
                switch (n->tracks[i].style) {
                    case NORDLICHT_STYLE_THUMBNAILS:
                        column = image_scale(frame, thumbnail_width, n->tracks[i].height);
                        break;
                    case NORDLICHT_STYLE_HORIZONTAL:
                        column = image_scale(frame, 1, n->tracks[i].height);
                        break;
                    case NORDLICHT_STYLE_VERTICAL:
                        tmp = image_scale(frame, n->tracks[i].height, 1);
                        column = image_flip(tmp);
                        image_free(tmp);
                        break;
                    case NORDLICHT_STYLE_SLITSCAN:
                        tmp = image_column(frame, 1.0*(x%thumbnail_width)/thumbnail_width);

                        column = image_scale(tmp, 1, n->tracks[i].height);
                        image_free(tmp);
                        break;
                    case NORDLICHT_STYLE_MIDDLECOLUMN:
                        tmp = image_column(frame, 0.5);
                        column = image_scale(tmp, 1, n->tracks[i].height);
                        image_free(tmp);
                        break;
                    case NORDLICHT_STYLE_SPECTROGRAM:
                        column = image_scale(frame, 1, n->tracks[i].height);
                        break;
                    default:
                        // cannot happen (TM)
                        return -1;
                        break;
                }

                image_to_bgra(n->data, n->width, n->height, column, x, y_offset);

                n->progress = (i+1.0*x/n->width)/n->num_tracks;
                x = x + image_width(column) - 1;

                image_free(column);
            }

            y_offset += n->tracks[i].height;
        }
    }

    n->progress = 1.0;
    return 0;
}

int nordlicht_write(const nordlicht *n, const char *filename) {
    int code = 0;

    if (filename == NULL) {
        error("Output filename must not be NULL");
        return -1;
    }

    if (strcmp(filename, "") == 0) {
        error("Output filename must not be empty");
        return -1;
    }

    char *realpath_output = realpath(filename, NULL);
    if (realpath_output != NULL) {
        // output file exists
        char *realpath_input = realpath(n->filename, NULL);
        if (realpath_input != NULL) {
            // otherwise, input filename is probably a URL

            if (strcmp(realpath_input, realpath_output) == 0) {
                error("Will not overwrite input file");
                code = -1;
            }
            free(realpath_input);
        }
        free(realpath_output);

        if (code != 0) {
            return code;
        }
    }

    image *i = image_from_bgra(n->data, n->width, n->height);
    if (image_write_png(i, filename) != 0) {
        return -1;
    }
    image_free(i);

    return code;
}

float nordlicht_progress(const nordlicht *n) {
    return n->progress;
}

const unsigned char* nordlicht_buffer(const nordlicht *n) {
    return n->data;
}

int nordlicht_set_buffer(nordlicht *n, unsigned char *data) {
    if (! n->modifiable) {
        return -1;
    }

    if (data == NULL) {
        return -1;
    }

    if (n->owns_data) {
        free(n->data);
    }
    n->owns_data = 0;
    n->data = data;
    return 0;
}
