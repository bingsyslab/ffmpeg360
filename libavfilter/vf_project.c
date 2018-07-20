/*
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * video project filter
 */

#include <stdio.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/eval.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/libm.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "gl_utils.h"
#include <png.h>

static const GLfloat back_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
static const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };

#define ONE_THIRD (1.0f/3)
#define TWO_THIRDS (2.0f/3)

static const char *const var_names[] = {
    "in_w", "iw",   ///< width  of the input video
    "in_h", "ih",   ///< height of the input video
    "out_w", "ow",  ///< width  of the projected video
    "out_h", "oh",  ///< height of the projected video
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    "x",
    "y",
    "n",            ///< number of frame
    "pos",          ///< position in the file
    "t",            ///< timestamp expressed in seconds
    "fovx",
    "fovy",
    "xr",
    "yr",
    "zr",
    NULL
};

enum var_name {
    VAR_IN_W,  VAR_IW,
    VAR_IN_H,  VAR_IH,
    VAR_OUT_W, VAR_OW,
    VAR_OUT_H, VAR_OH,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VAR_X,
    VAR_Y,
    VAR_N,
    VAR_POS,
    VAR_T,
    VAR_FOVX,
    VAR_FOVY,
    VAR_XR,
    VAR_YR,
    VAR_ZR,
    VAR_VARS_NB
};

typedef struct _tile {
    double x;
    double y;
    double z;
    double fovx;
    double fovy;
    double u;
    double v;
    double w;
    double h;
}tile_t;

typedef struct ProjectContext {
    const AVClass *class;
    int  x;             ///< x offset of the non-projected area with respect to the input area
    int  y;             ///< y offset of the non-projected area with respect to the input area
    int  w;             ///< width of the projected area
    int  h;             ///< height of the projected area
    int iw, ih;

    AVRational out_sar; ///< output sample aspect ratio
    int keep_aspect;    ///< keep display aspect ratio when projecting
    int exact;          ///< exact projecting, for subsampled formats

    int max_step[4];    ///< max pixel step for each plane, expressed as a number of bytes
    int hsub, vsub;     ///< chroma subsampling
    char *x_expr, *y_expr, *w_expr, *h_expr;
    AVExpr *x_pexpr, *y_pexpr;  /* parsed expressions for x and y */
    double var_values[VAR_VARS_NB];

    double fovx, fovy;
    double xr, yr, zr;
    char *vshader;
    char *fshader;
    char *orfile;
    vector_t *ors;
    double tb; // time base
    double ecoef;

    char *lofile;
    vector_t *layout;
    tile_t *tiles;
    Vertex *vertices;

    // OpenGL
    Matrix ModelMatrix;
    Matrix ProjectionMatrix;
    Matrix ViewMatrix;

    GLuint ProjectionMatrixUniformLocation;
    GLuint ViewMatrixUniformLocation;
    GLuint ModelMatrixUniformLocation;
    GLuint ResolutionUniformLocation;
    GLuint FovUniformLocation;
    GLuint YawUniformLocation;
    GLuint PitchUniformLocation;
    GLuint RollUniformLocation;
    GLuint ShaderIds[3];
    GLuint BufferIds[4];

    GLuint TextureId;

    GLuint FramebufferId;
    GLuint RenderbufferId;

    GLuint FramebufferId2;
    GLuint RenderbufferId2;

    // GLFW window handle
    GLFWwindow* WindowHandle;

    // store the original data from frames as texture
    uint8_t *ori_buffer[3];

} ProjectContext;

static av_cold void uninit(AVFilterContext *ctx);

int CreateTiles(AVFilterContext *ctx);
int DrawTiles(AVFilterContext *ctx, double rotations[3], const GLfloat res[2]);
void DestroyCube(AVFilterContext *ctx);
int CreateTexutre(AVFilterContext *ctx);
void LoadTexture(AVFilterContext *ctx, int w, int h, uint8_t *img);
void DestroyTexture(AVFilterContext *ctx);
void CreateFramebuffer(AVFilterContext *ctx, int w, int h);
void CreateFramebuffer2(AVFilterContext *ctx, int w, int h);
void DestroyFramebuffer(AVFilterContext *ctx);
void printPixelFormat(AVFilterContext *ctx, const AVPixFmtDescriptor *desc);

void write_png_file(char *filename, int w, int h, uint8_t *d);

static int readLine(FILE *fp, char *buf, int size)
{
    char ch;
    int count = 0;

    while( ++count <= size && ((ch = getc(fp)) != '\n') ){
        if( ch == EOF ){
            count--;
            return count;
        }
        buf[count - 1] = ch;
    }
    buf[count - 1] = '\0';

    return count;
}

static int parseArgsf(char *line, double *args, const char *del)
{
    int parsed = 0;
    char *pt;
    pt = strtok(line, del);
    while(pt != NULL){
        parsed++;
        args[parsed-1] = atof(pt);
        pt = strtok(NULL, del);
    }
    return parsed;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM)) &&
            !((desc->log2_chroma_w || desc->log2_chroma_h) && !(desc->flags & AV_PIX_FMT_FLAG_PLANAR)) &&
            (ret = ff_add_format(&formats, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, formats);
}

/* void errorCallback(int errno, const char * description) */
/* { */
/*     fprintf(stderr, "Error: %s\n", description); */
/* } */

static int InitWindow(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    /* glfwSetErrorCallback(errorCallback); */
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    s->WindowHandle = glfwCreateWindow (640, 640, "OpenGL", NULL, NULL);
    if (! s->WindowHandle) {
      av_log(ctx, AV_LOG_ERROR, "[OpenGL] ERROR: could not open window with GLFW3\n");
      glfwTerminate ();
      return -1;
    }
    glfwMakeContextCurrent (s->WindowHandle);
 
    return 0;
}

static int gl_init(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;
    GLenum GlewInitResult;

    if(InitWindow(ctx))
        return -1;

    glewExperimental = GL_TRUE;
    GlewInitResult = glewInit();
    if(GLEW_OK != GlewInitResult){
        av_log(ctx, AV_LOG_ERROR, "[OpenGL] GLEW initialization failed: %s\n", glewGetErrorString(GlewInitResult));
        return -1;
    }

    av_log(ctx, AV_LOG_INFO, "[OpenGL] OpenGL Version: %s\n", glGetString(GL_VERSION));

    glGetError();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    if(CheckGLError(ctx, "ERROR: Could not set OpenGL depth testing options"))
        return -1;

    if(CreateTexutre(ctx))
        return -1;

    s->ModelMatrix = IDENTITY_MATRIX;
    s->ProjectionMatrix = IDENTITY_MATRIX;
    s->ViewMatrix = IDENTITY_MATRIX;

    memset(s->ShaderIds, 0, sizeof(s->ShaderIds));
    memset(s->BufferIds, 0, sizeof(s->BufferIds));
    s->TextureId = 0;
    s->FramebufferId = 0;
    s->RenderbufferId = 0;
    s->FramebufferId2 = 0;
    s->RenderbufferId2 = 0;

    return 0;
}


static av_cold int init(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;
    int i;

    av_log(ctx, AV_LOG_INFO, "[Project Filter] Initializing project filter...\n");

    for(i = 0; i < 3; i++)
        s->ori_buffer[i] = NULL;
    s->ors = init_vector();
    s->layout = init_vector();

    av_log(ctx, AV_LOG_INFO, "[Project Filter] Initialize OpenGL context\n");
    if(gl_init(ctx))
        return AVERROR(ENOSYS);

    av_log(ctx, AV_LOG_INFO, "[Project Filter] Initialization done\n");
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;
    int i;

    av_log(ctx, AV_LOG_INFO, "[Project Filter] uninit(): Uninitializing project filter...\n");

    DestroyCube(ctx);
    DestroyFramebuffer(ctx);
    DestroyTexture(ctx);

    if(s->layout->nr > 0){
        free(s->tiles);
        free(s->vertices);
    }

    destroy_vector(s->ors);
    destroy_vector(s->layout);

    av_expr_free(s->x_pexpr);
    s->x_pexpr = NULL;
    av_expr_free(s->y_pexpr);
    s->y_pexpr = NULL;

    for(i = 0; i < 3; i++)
        if(s->ori_buffer[i] != NULL)
            free(s->ori_buffer[i]);
}

static inline int normalize_double(int *n, double d)
{
    int ret = 0;

    if (isnan(d)) {
        ret = AVERROR(EINVAL);
    } else if (d > INT_MAX || d < INT_MIN) {
        *n = d > INT_MAX ? INT_MAX : INT_MIN;
        ret = AVERROR(EINVAL);
    } else
        *n = lrint(d);

    return ret;
}

static av_cold int load_orfile(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    FILE *fp;
    char line[128];
    int ret;
    vector_item_t item;

    s->ors = init_vector();

    if(strcmp(s->orfile, "")){
        av_log(ctx, AV_LOG_INFO, "[Project Filter] load_rofile(): Read head orientations from %s\n", s->orfile);
        fp = fopen(s->orfile, "r");
        if(fp == NULL){
            av_log(ctx, AV_LOG_ERROR, "[Project Filter] load_orfile(): Failed to open file %s\n", s->orfile);
            return EIO;
        }

        while( (ret = readLine(fp, line, 128)) > 0 ){
            memcpy(item.str, line, 128);
            push_back(s->ors, item);
        }

        //pr_items_str(s->ors);
        fclose(fp);
    }

    return 0;
}

static const char *cube_layout[6] = {
    "0.333333:0.5:90:90:0:0:0:0.333333:0.5",
    "0.333333:0.5:90:90:90:0:0:0.666667:0",
    "0.333333:0.5:90:90:-90:0:0:0:0.5",
    "0.333333:0.5:90:90:0:90:0:0:0",
    "0.333333:0.5:90:90:0:-90:0:0.333333:0",
    "0.333333:0.5:90:90:0:180:0:0.666667:0.5",
};

static av_cold int load_lofile(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    FILE *fp;
    char line[128];
    int ret, i;
    vector_item_t item;

    if(strcmp(s->lofile, "")){
        av_log(ctx, AV_LOG_INFO, "[Project Filter] load_lofile(): read layout from %s\n", s->lofile);

        const char* layout_dir = "ffmpeg360_layout/";
        const size_t layout_path_length = strlen(layout_dir) + strlen(s->lofile) + 1;
        char* layout_path = malloc(layout_path_length);

        snprintf(layout_path, layout_path_length, "%s%s", layout_dir, s->lofile);

        fp = fopen(layout_path, "r");
        free(layout_path);

        if(fp == NULL){
            av_log(ctx, AV_LOG_ERROR, "[Project Filter] load_lofile(): failed to open file %s\n", s->lofile);
            return EIO;
        }

        while( (ret = readLine(fp, line, 128)) > 0 ){
            memcpy(item.str, line, 128);
            push_back(s->layout, item);
        }

        //pr_items_str(s->layout);
        fclose(fp);
    }else{
        for(i = 0; i < 6; i++){
            memcpy(item.str, cube_layout[i], 128);
            push_back(s->layout, item);
        }
    }
    return 0;
}

static av_cold int parse_tiles(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    char line[128];
    int parsed, i;
    double tile_args[9];

    if(s->layout->nr == 0){
        av_log(ctx, AV_LOG_ERROR, "[Project Filter] no tile representation to parse!\n");
        return EINVAL;
    }

    s->tiles = malloc(sizeof(tile_t) * s->layout->nr);

    for(i = 0; i < s->layout->nr; i++){
        memcpy(line, s->layout->head[i].str, 128);
        parsed = parseArgsf(line, tile_args, ":");
        // every line: w:h:fovx:fovy:xr:yr:zr:u:v
        if(parsed != 9){
            av_log(ctx, AV_LOG_ERROR, "[Project Filter] Error on parsing layout file %s line %d: %s\n", s->lofile, i+1, s->layout->head[i].str);
            av_log(ctx, AV_LOG_ERROR, "[Project Filter] Parsed result: %d - %f %f %f %f %f %f %f %f %f\n",
                   parsed, tile_args[0], tile_args[1], tile_args[2], tile_args[3], tile_args[4], tile_args[5],
                   tile_args[6], tile_args[7], tile_args[8]);
            return EINVAL;
        }

        s->tiles[i].w = tile_args[0];
        s->tiles[i].h = tile_args[1];
        s->tiles[i].fovx = tile_args[2];
        s->tiles[i].fovy = tile_args[3];
        s->tiles[i].x = tile_args[4];
        s->tiles[i].y = tile_args[5];
        s->tiles[i].z = tile_args[6];
        s->tiles[i].u = tile_args[7];
        s->tiles[i].v = tile_args[8];

        av_log(ctx, AV_LOG_DEBUG, "[Project Filter] Tile parameters (x, y, z, fovx, fovy, u, v): %f, %f, %f, %f, %f, %f, %f, %f, %f\n",
               s->tiles[i].x, s->tiles[i].y, s->tiles[i].z, s->tiles[i].fovx, s->tiles[i].fovy, s->tiles[i].w, s->tiles[i].h, s->tiles[i].u, s->tiles[i].v);
    }

    return 0;
}

static int config_input(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    ProjectContext *s = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(link->format);
    int ret, i;
    const char *expr;
    double res;
    size_t bufsize;
    double fovx, fovy;

    av_log(ctx, AV_LOG_INFO, "[Project Filter] Configuring input parameters...\n");

    av_log(ctx, AV_LOG_INFO, "[Project Filter]   pixel format: %s\n", pix_desc->alias);
    printPixelFormat(ctx, pix_desc);

    av_log(ctx, AV_LOG_INFO, "[Project Filter]   fovx=%f, fovy=%f, xr=%f, yr=%f, zr=%f, orfile='%s', lofile='%s', vshader='%s', fshader='%s'\n",
           s->fovx, s->fovy, s->xr, s->yr, s->zr, s->orfile, s->lofile, s->vshader, s->fshader);

    s->var_values[VAR_IN_W]  = s->var_values[VAR_IW] = ctx->inputs[0]->w;
    s->var_values[VAR_IN_H]  = s->var_values[VAR_IH] = ctx->inputs[0]->h;
    s->var_values[VAR_A]     = (float) link->w / link->h;
    s->var_values[VAR_SAR]   = link->sample_aspect_ratio.num ? av_q2d(link->sample_aspect_ratio) : 1;
    s->var_values[VAR_DAR]   = s->var_values[VAR_A] * s->var_values[VAR_SAR];
    s->var_values[VAR_HSUB]  = 1<<pix_desc->log2_chroma_w;
    s->var_values[VAR_VSUB]  = 1<<pix_desc->log2_chroma_h;
    s->var_values[VAR_X]     = NAN;
    s->var_values[VAR_Y]     = NAN;
    s->var_values[VAR_OUT_W] = s->var_values[VAR_OW] = NAN;
    s->var_values[VAR_OUT_H] = s->var_values[VAR_OH] = NAN;
    s->var_values[VAR_N]     = 0;
    s->var_values[VAR_T]     = NAN;
    s->var_values[VAR_POS]   = NAN;

    av_image_fill_max_pixsteps(s->max_step, NULL, pix_desc);
    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;

    normalize_double(&s->iw, s->var_values[VAR_IN_W]);
    normalize_double(&s->ih, s->var_values[VAR_IN_H]);

    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, s->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail_expr;
    s->var_values[VAR_OUT_W] = s->var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                      var_names, s->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail_expr;
    s->var_values[VAR_OUT_H] = s->var_values[VAR_OH] = res;
    /* evaluate again ow as it may depend on oh */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, s->var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail_expr;

    s->var_values[VAR_OUT_W] = s->var_values[VAR_OW] = res;
    if (normalize_double(&s->w, s->var_values[VAR_OUT_W]) < 0 ||
        normalize_double(&s->h, s->var_values[VAR_OUT_H]) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Too big value or invalid expression for out_w/ow or out_h/oh. "
               "Maybe the expression for out_w:'%s' or for out_h:'%s' is self-referencing.\n",
               s->w_expr, s->h_expr);
        return AVERROR(EINVAL);
    }

    if (!s->exact) {
        s->w &= ~((1 << s->hsub) - 1);
        s->h &= ~((1 << s->vsub) - 1);
    }

    av_expr_free(s->x_pexpr);
    av_expr_free(s->y_pexpr);
    s->x_pexpr = s->y_pexpr = NULL;
    if ((ret = av_expr_parse(&s->x_pexpr, s->x_expr, var_names,
                             NULL, NULL, NULL, NULL, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&s->y_pexpr, s->y_expr, var_names,
                             NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return AVERROR(EINVAL);

    if (s->keep_aspect) {
        AVRational dar = av_mul_q(link->sample_aspect_ratio,
                                  (AVRational){ link->w, link->h });
        av_reduce(&s->out_sar.num, &s->out_sar.den,
                  dar.num * s->h, dar.den * s->w, INT_MAX);
    } else
        s->out_sar = link->sample_aspect_ratio;

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d sar:%d/%d -> w:%d h:%d sar:%d/%d\n",
           link->w, link->h, link->sample_aspect_ratio.num, link->sample_aspect_ratio.den,
           s->w, s->h, s->out_sar.num, s->out_sar.den);

    if (s->w <= 0 || s->h <= 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid non positive size for width '%d' or height '%d'\n",
               s->w, s->h);
        return AVERROR(EINVAL);
    }

    /* set default, required in the case the first computed value for x/y is NAN */
    s->x = (link->w - s->w) / 2;
    s->y = (link->h - s->h) / 2;
    if (!s->exact) {
        s->x &= ~((1 << s->hsub) - 1);
        s->y &= ~((1 << s->vsub) - 1);
    }

    if(s->ecoef != 1.0f){
        fovx = s->fovx;
        fovy = s->fovy;
        s->fovx = RadiansToDegrees( atan2( tan(DegreesToRadians(s->fovx / 2.0)) * s->ecoef, 1.0 ) ) * 2;
        s->fovy = RadiansToDegrees( atan2( tan(DegreesToRadians(s->fovy / 2.0)) * s->ecoef, 1.0 ) )* 2;
        av_log(ctx, AV_LOG_INFO, "[Project Filter] expand fovx, fovy from %.2f, %.2f to %.2f, %.2f with expand coefficient %.2f\n", fovx, fovy, s->fovx, s->fovy, s->ecoef);
    }

    // configure the width and height of framebuffer
    av_log(ctx, AV_LOG_INFO, "[Project Filter] configure the framebuffer width and height as %d and %d\n", s->w, s->h);
    /* CreateFramebuffer(ctx, s->w, s->h); */
    /* CreateFramebuffer2(ctx, (s->w >> s->hsub), (s->h >> s->vsub)); */
    glGenFramebuffers(1, &s->FramebufferId);
    glGenRenderbuffers(1, &s->RenderbufferId);
    glGenFramebuffers(1, &s->FramebufferId2);
    glGenRenderbuffers(1, &s->RenderbufferId2);



    for(i = 0; i < 3; i++){
        bufsize = sizeof(uint8_t);
        if(i == 0){
            bufsize *= s->iw;
            bufsize *= s->ih;
        }else{
            bufsize *= (s->iw >> s->hsub);
            bufsize *= (s->ih >> s->vsub);
        }
        if(s->ori_buffer[i] != NULL)
            free(s->ori_buffer[i]);
        s->ori_buffer[i] = malloc(bufsize);
    }

    // load orientation file
    if(ret = load_orfile(ctx))
        return AVERROR(ret);

    // load from layout file or the default cubic layout
    if(ret = load_lofile(ctx))
        return AVERROR(ret);

    // parse the tile layout
    if(ret = parse_tiles(ctx))
        return AVERROR(ret);

    if(ret = CreateTiles(ctx))
        return AVERROR(ret);

    return 0;

fail_expr:
    av_log(NULL, AV_LOG_ERROR, "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static int config_output(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ProjectContext *s = link->src->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(link->format);

    av_log(link->src, AV_LOG_INFO, "[Project Filter] Entrance of config_output\n");

    av_log(ctx, AV_LOG_INFO, "[Project Filter] pixel format: %s\n", pix_desc->alias);
    printPixelFormat(ctx, pix_desc);

    link->w = s->w;
    link->h = s->h;
    link->sample_aspect_ratio = s->out_sar;

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    ProjectContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    int ret;
    int i;
    int j;
    int in_w, in_h;
    static int fr_idx = 0;
    // time in sec
    double fr_t, args[4], rotations[3];
    int parsed;
    char line[128];
    const GLfloat res[2] = { s->w, s->h };
    const GLfloat res2[2] = { s->w >> s->hsub, s->h >> s->vsub };


    fr_idx++;
    if(fr_idx == 1)
        av_log(ctx, AV_LOG_INFO, "[Project Filter] filter_frame(): frame %d\n", fr_idx);

    fr_t = frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(link->time_base);
    if(fr_idx == 1)
        av_log(ctx, AV_LOG_INFO, "[Project Filter] filter_frame(): frame: %d, pts: %ld, timestamp: %ld, time: %f, timebase: %f\n", fr_idx, frame->pts, frame->best_effort_timestamp, fr_t, s->tb);

    rotations[0] = s->xr;
    rotations[1] = s->yr;
    rotations[2] = s->zr;

    if(s->ors->nr > 0){
        for(i = 0; i < s->ors->nr; i++){
            memcpy(line, s->ors->head[i].str, 128);
            parsed = parseArgsf(line, args, " ");
            //printf("parsed: %d, args: %f, %f, %f, %f\n", parsed, args[0], args[1], args[2], args[3]);
            if(parsed != 4){
                av_log(ctx, AV_LOG_ERROR, "[Project Filter] Error on parsing file %s line %d: %s\n", s->orfile, i+1, s->ors->head[i].str);
                return AVERROR(ENOSYS);
            }

            if(args[0] > fr_t + s->tb)
                break;
            rotations[0] = args[2];
            rotations[1] = args[3];
            rotations[2] = args[4];
        }
    }

    // av_log(ctx, AV_LOG_INFO, "[Project Filter] filter_frame(): rotation about x: %f, y: %f, z: %f\n", rotations[0], rotations[1], rotations[2]);

    in_w = frame->width;
    in_h = frame->height;

    frame->width  = s->w;
    frame->height = s->h;

    s->var_values[VAR_N] = link->frame_count_out;
    s->var_values[VAR_T] = frame->pts == AV_NOPTS_VALUE ?
        NAN : frame->pts * av_q2d(link->time_base);
    s->var_values[VAR_POS] = frame->pkt_pos == -1 ?
        NAN : frame->pkt_pos;
    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);
    s->var_values[VAR_Y] = av_expr_eval(s->y_pexpr, s->var_values, NULL);
    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);

    normalize_double(&s->x, s->var_values[VAR_X]);
    normalize_double(&s->y, s->var_values[VAR_Y]);

    if (s->x < 0)
        s->x = 0;
    if (s->y < 0)
        s->y = 0;
    if ((unsigned)s->x + (unsigned)s->w > link->w)
        s->x = link->w - s->w;
    if ((unsigned)s->y + (unsigned)s->h > link->h)
        s->y = link->h - s->h;
    if (!s->exact) {
        s->x &= ~((1 << s->hsub) - 1);
        s->y &= ~((1 << s->vsub) - 1);
    }

    s->x = s->y = 0;

    frame->data[0] += s->y * frame->linesize[0];
    frame->data[0] += s->x * s->max_step[0];

    if (!(desc->flags & AV_PIX_FMT_FLAG_PAL || desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)) {
        for (i = 1; i < 3; i ++) {
            if (frame->data[i]) {
                frame->data[i] += (s->y >> s->vsub) * frame->linesize[i];
                frame->data[i] += (s->x * s->max_step[i]) >> s->hsub;
            }
        }
    }

    /* alpha plane */
    if (frame->data[3]) {
        frame->data[3] += s->y * frame->linesize[3];
        frame->data[3] += s->x * s->max_step[3];
    }


    if(fr_idx == 1)
        av_log(ctx, AV_LOG_INFO, "[Project Filter] s->iw: %d, s->ih: %d, s->hsub: %d, s->vsub: %d, frame->linesize[0]: %d, frame->linesize[1]: %d, frame->linesize[2]: %d\n",
               s->iw, s->ih, s->hsub, s->vsub, frame->linesize[0], frame->linesize[1], frame->linesize[2]);

    for(i = 0; i < 3; i++){
        if(i == 0){
            if(fr_idx == 1)
                av_log(ctx, AV_LOG_INFO, "[Project Filter] copy data to %p with size %lu, from %p with size %d\n",
                       s->ori_buffer[i], sizeof(uint8_t) * s->iw * s->ih,
                       frame->data[i], frame->linesize[i] * in_h
                );
            for (j = 0; j < s->ih; j++) {
                memcpy(s->ori_buffer[i] + j * s->iw, frame->data[i] + j * frame->linesize[0],
                           sizeof(uint8_t) * s->iw);
            }
        }else{
            if(fr_idx == 1)
                av_log(ctx, AV_LOG_INFO, "[Project Filter] copy data to %p with size %lu, from %p with size %d\n",
                   s->ori_buffer[i], sizeof(uint8_t) * (s->iw >> s->hsub) * (s->ih >> s->vsub),
                   frame->data[i], frame->linesize[i] * (in_h >> s->vsub)
                );
            for (j = 0; j < (s->ih >> s->vsub); j++) {
                memcpy(s->ori_buffer[i] + j * (s->iw >> s->hsub),
                       frame->data[i] + j * frame->linesize[i],
                       sizeof(uint8_t) * (s->iw >> s->hsub));
            }
        }
    }


    /* const GLfloat back_color[] = { 0.0f, 0.0f, 0.0f, 1.0f }; */
    /* const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 }; */

    glViewport(0, 0, s->w, s->h);

    LoadTexture(ctx, in_w, in_h, s->ori_buffer[0]);
    glBindTexture(GL_TEXTURE_2D, s->TextureId);


    CreateFramebuffer(ctx, s->w, s->h);
    glBindFramebuffer(GL_FRAMEBUFFER, s->FramebufferId);
    ExitOnGLError(ctx, "ERROR: Could not bind frame buffer");
    glDrawBuffers(1, draw_buffers);
    glClearBufferfv(GL_COLOR, 0, back_color);
    ExitOnGLError(ctx, "ERROR: Could not setup clear buffer");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ExitOnGLError(ctx, "ERROR: Could not clear frame buffer");

    if(ret = DrawTiles(ctx, rotations, res))
        return AVERROR(ret);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    ExitOnGLError(ctx, "ERROR: Could not read buffer");

    frame->width = s->w;
    frame->height = s->h;
    frame->linesize[0] = s->w;
    frame->linesize[1] = s->w >> s->vsub;
    frame->linesize[2] = s->w >> s->vsub;

    av_frame_get_buffer(frame, 1);

    if(fr_idx == 1)
      av_log(ctx, AV_LOG_INFO, "[Project Filter] parameters: s->max_step: %d, %d, %d, linesize: %d, %d, %d, w/h: %d, %d, hsub/vsub: %d, %d\n",
             s->max_step[0], s->max_step[1], s->max_step[2], frame->linesize[0], frame->linesize[1], frame->linesize[2],
             s->w, s->h, s->vsub, s->hsub);

    glReadPixels(0, 0, s->w, s->h, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    ExitOnGLError(ctx, "ERROR: Could not read pixel");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // u plane
    glViewport(0, 0, s->w >> s->hsub, s->h >> s->vsub);

    LoadTexture(ctx, in_w >> s->hsub, in_h >> s->vsub, s->ori_buffer[1]);
    glBindTexture(GL_TEXTURE_2D, s->TextureId);

    CreateFramebuffer2(ctx, (s->w >> s->hsub), (s->h >> s->vsub));
    glBindFramebuffer(GL_FRAMEBUFFER, s->FramebufferId2);
    ExitOnGLError(ctx, "ERROR: Could not bind frame buffer 2");
    glDrawBuffers(1, draw_buffers);
    glClearBufferfv(GL_COLOR, 0, back_color);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ExitOnGLError(ctx, "ERROR: Could not clear frame buffer 2");

    if(ret = DrawTiles(ctx, rotations, res2))
        return AVERROR(ret);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    ExitOnGLError(ctx, "ERROR: Could not read buffer");

    frame->linesize[1] = s->w >> s->hsub;
    glReadPixels(0, 0, s->w >> s->hsub, s->h >> s->vsub, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
    ExitOnGLError(ctx, "ERROR: Could not read pixel");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // v plane
    glViewport(0, 0, s->w >> s->hsub, s->h >> s->vsub);

    LoadTexture(ctx, in_w >> s->hsub, in_h >> s->vsub, s->ori_buffer[2]);
    glBindTexture(GL_TEXTURE_2D, s->TextureId);

    CreateFramebuffer2(ctx, (s->w >> s->hsub), (s->h >> s->vsub));
    glBindFramebuffer(GL_FRAMEBUFFER, s->FramebufferId2);
    ExitOnGLError(ctx, "ERROR: Could not bind frame buffer 2");
    glDrawBuffers(1, draw_buffers);
    glClearBufferfv(GL_COLOR, 0, back_color);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ExitOnGLError(ctx, "ERROR: Could not clear frame buffer 2");

    if(ret = DrawTiles(ctx, rotations, res2))
        return AVERROR(ret);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    ExitOnGLError(ctx, "ERROR: Could not read buffer");

    frame->linesize[2] = s->w >> s->hsub;
    glReadPixels(0, 0, s->w >> s->hsub, s->h >> s->vsub, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
    ExitOnGLError(ctx, "ERROR: Could not read pixel");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    if(frame->data[3])
        memset(frame->data[3], 255, frame->height * frame->linesize[3]);

    //glUseProgram(0);


    return ff_filter_frame(link->dst->outputs[0], frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    ProjectContext *s = ctx->priv;
    int ret;

    av_log(ctx, AV_LOG_INFO, "[Project Filter] process_command(): processing the command...\n");

    if (   !strcmp(cmd, "out_w")  || !strcmp(cmd, "w")
        || !strcmp(cmd, "out_h")  || !strcmp(cmd, "h")
        || !strcmp(cmd, "x")      || !strcmp(cmd, "y")) {

        int old_x = s->x;
        int old_y = s->y;
        int old_w = s->w;
        int old_h = s->h;

        AVFilterLink *outlink = ctx->outputs[0];
        AVFilterLink *inlink  = ctx->inputs[0];

        av_opt_set(s, cmd, args, 0);

        if ((ret = config_input(inlink)) < 0) {
            s->x = old_x;
            s->y = old_y;
            s->w = old_w;
            s->h = old_h;
            return ret;
        }

        ret = config_output(outlink);

    } else
        ret = AVERROR(ENOSYS);

    return ret;
}

#define OFFSET(x) offsetof(ProjectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption project_options[] = {
    { "out_w",       "set the width project area expression",   OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "w",           "set the width project area expression",   OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "out_h",       "set the height project area expression",  OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "h",           "set the height project area expression",  OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "fovx",        "set horizontal degree of FOV",            OFFSET(fovx), AV_OPT_TYPE_DOUBLE,   {.dbl = 90.0},   0, 179.0, FLAGS },
    { "fovy",        "set vertical degree of FOV",              OFFSET(fovy), AV_OPT_TYPE_DOUBLE,   {.dbl = 90.0},   0, 179.0, FLAGS },
    { "xr",          "set rotation by x-axis",                  OFFSET(xr), AV_OPT_TYPE_DOUBLE,     {.dbl = 0},    -360, 360, FLAGS },
    { "yr",          "set rotation by y-axis",                  OFFSET(yr), AV_OPT_TYPE_DOUBLE,     {.dbl = 0},    -360,  360, FLAGS },
    { "zr",          "set rotation by z-axis",                  OFFSET(zr), AV_OPT_TYPE_DOUBLE,     {.dbl = 0},    -360,  360, FLAGS },
    { "vshader",     "set the vertex shader path",              OFFSET(vshader), AV_OPT_TYPE_STRING, {.str = ""},  CHAR_MIN, CHAR_MAX, FLAGS },
    { "fshader",     "set the fragment shader path",            OFFSET(fshader), AV_OPT_TYPE_STRING, {.str = ""},  CHAR_MIN, CHAR_MAX, FLAGS },
    { "orfile",      "set the orientation file",                OFFSET(orfile), AV_OPT_TYPE_STRING, {.str = ""},   CHAR_MIN, CHAR_MAX, FLAGS },
    { "lofile",      "set the layout file",                     OFFSET(lofile), AV_OPT_TYPE_STRING, {.str = ""},   CHAR_MIN, CHAR_MAX, FLAGS },
    { "timebase",    "set time base for loading orientation",   OFFSET(tb), AV_OPT_TYPE_DOUBLE,     {.dbl = 0},    0, 999999, FLAGS },
    { "ecoef",       "set expansion coefficient",               OFFSET(ecoef), AV_OPT_TYPE_DOUBLE,  {.dbl = 1.0},  0.8,1.2, FLAGS},
    { "x",           "set the x project area expression",       OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "(in_w-out_w)/2"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "y",           "set the y project area expression",       OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "(in_h-out_h)/2"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "keep_aspect", "keep aspect ratio",                       OFFSET(keep_aspect), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "exact",       "do exact projecting",                     OFFSET(exact),  AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(project);

static const AVFilterPad avfilter_vf_project_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_project_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_project = {
    .name            = "project",
    .description     = NULL_IF_CONFIG_SMALL("Project the input cubic layout video."),
    .priv_size       = sizeof(ProjectContext),
    .priv_class      = &project_class,
    .query_formats   = query_formats,
    .uninit          = uninit,
    .init            = init,
    .inputs          = avfilter_vf_project_inputs,
    .outputs         = avfilter_vf_project_outputs,
    .process_command = process_command,
};

int CreateTiles(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;
    GLint logSize = 0;
    GLchar *log = NULL;
    int i, j;
    double px, py, pz, pu, pv;
    double lx, rx, ty, by; // left_x, right_x, top_y, bottom_y
    Matrix rotation;

    av_log(ctx, AV_LOG_INFO, "[Project Filter] Creating Tiles......\n");
    av_log(ctx, AV_LOG_INFO, "[Project Filter] \n");

    // each tile is drawn by 6 vertices
    s->vertices = malloc(sizeof(Vertex) * 6 * s->layout->nr);
    for(i = 0; i < s->layout->nr; i++){
        /* Create tile vertices here */
        /* use the args in s->tiles[i], which are x, y, z, fovx, fovy, u, v */

        lx = -1 * tan( DegreesToRadians( s->tiles[i].fovx / 2 ) );
        rx = -1 * lx;
        ty = tan( DegreesToRadians( s->tiles[i].fovy / 2 ) );
        by = -1 * ty;
        /* av_log(ctx, AV_LOG_INFO, "[Project Filter]\n Before applying rotation, the corners are: (%.2f, %.2f) , (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)\n", */
        /*        lx, ty,   lx, by,   rx, ty,   rx, by); */

        rotation = IDENTITY_MATRIX;

        RotateAboutY(&rotation, DegreesToRadians(s->tiles[i].y));
        RotateAboutX(&rotation, DegreesToRadians(s->tiles[i].x));
        RotateAboutZ(&rotation, DegreesToRadians(s->tiles[i].z));

        av_log(ctx, AV_LOG_DEBUG, "\n");
        for(j = 0; j < 6; j++){
            pz = -1.0f;
            switch(j){
            case 0:
            case 3:
                px = lx;
                py = by;
                pu = s->tiles[i].u;
                pv = s->tiles[i].v;
                break;
            case 1:
                px = rx;
                py = by;
                pu = s->tiles[i].u + s->tiles[i].w;
                pv = s->tiles[i].v;
                break;
            case 2:
            case 4:
                px = rx;
                py = ty;
                pu = s->tiles[i].u + s->tiles[i].w;
                pv = s->tiles[i].v + s->tiles[i].h;
                break;
            case 5:
                px = lx;
                py = ty;
                pu = s->tiles[i].u;
                pv = s->tiles[i].v + s->tiles[i].h;
                break;
            }
            s->vertices[i * 6 + j].position[0] = px;
            s->vertices[i * 6 + j].position[1] = py;
            s->vertices[i * 6 + j].position[2] = pz;
            s->vertices[i * 6 + j].position[3] = 1.0f;
            s->vertices[i * 6 + j].uv[0] = pu;
            s->vertices[i * 6 + j].uv[1] = pv;
            s->vertices[i * 6 + j].uvr[0] = s->tiles[i].u;
            s->vertices[i * 6 + j].uvr[1] = s->tiles[i].v;
            s->vertices[i * 6 + j].uvr[2] = s->tiles[i].w;
            s->vertices[i * 6 + j].uvr[3] = s->tiles[i].h;

            MultiplyVertex(&rotation, s->vertices + i * 6 + j);

            av_log(ctx, AV_LOG_DEBUG, "{ %.2f, %.2f, %.2f, %.2f }, { %.2f, %.2f }, { %.2f, %.2f, %.2f, %.2f }\n",
                   s->vertices[i * 6 + j].position[0], s->vertices[i * 6 + j].position[1],
                   s->vertices[i * 6 + j].position[2], s->vertices[i * 6 + j].position[3],
                   s->vertices[i * 6 + j].uv[0], s->vertices[i * 6 + j].uv[1],
                   s->vertices[i * 6 + j].uvr[0], s->vertices[i * 6 + j].uvr[1],
                   s->vertices[i * 6 + j].uvr[2], s->vertices[i * 6 + j].uvr[3]);
        }
        /* av_log(ctx, AV_LOG_INFO, "\n"); */

        av_log(ctx, AV_LOG_INFO, "[Project Filter]\n After applying rotation, the left-top corner is: (%.2f, %.2f, %.2f, %.2f)\n",
 s->vertices[i*6].position[0], s->vertices[i*6].position[1], s->vertices[i*6].position[2], s->vertices[i*6].position[3]);
    }

    glfwMakeContextCurrent (s->WindowHandle);
    // ShaderIds[3]: ProgramId, VertexShaderId, FragmentShaderId
    s->ShaderIds[0] = glCreateProgram();
    ExitOnGLError(ctx, "ERROR: Could not create the shader program");

    s->ShaderIds[1] = LoadShader(ctx, s->fshader, GL_FRAGMENT_SHADER);
    s->ShaderIds[2] = LoadShader(ctx, s->vshader, GL_VERTEX_SHADER);

    if(s->ShaderIds[1] == 0 || s->ShaderIds[2] == 0){
        av_log(ctx, AV_LOG_ERROR, "[Project Filter] Error on loading vertex/fragment shaders: ('%s'/'%s')\n", s->vshader, s->fshader);
        return AVERROR(ENOSYS);
    }

    glAttachShader(s->ShaderIds[0], s->ShaderIds[1]);
    glAttachShader(s->ShaderIds[0], s->ShaderIds[2]);

    //av_log(ctx, AV_LOG_INFO, "[OpenGL] INFO: program id %d, fragshader id %d, vertexshader id %d\n", s->ShaderIds[0], s->ShaderIds[1], s->ShaderIds[2]);

    // May add code to check the compiling/linking result for debugging
    glLinkProgram(s->ShaderIds[0]);
    ExitOnGLError(ctx, "ERROR: Could not link the shader program");

    if(GL_NO_ERROR != glGetError()){
        glGetProgramiv(s->ShaderIds[0], GL_INFO_LOG_LENGTH, &logSize);
        av_log(ctx, AV_LOG_INFO, "ERROR: use program failed. log length(%d)\n", logSize);
        log = malloc(logSize * sizeof(GLchar));
        glGetProgramInfoLog(s->ShaderIds[0], logSize, NULL, log);
        av_log(ctx, AV_LOG_INFO, "  use program error info: %s\n", log);
        free(log);
    }

    s->ModelMatrixUniformLocation = glGetUniformLocation(s->ShaderIds[0], "ModelMatrix");
    s->ViewMatrixUniformLocation = glGetUniformLocation(s->ShaderIds[0], "ViewMatrix");
    s->ProjectionMatrixUniformLocation = glGetUniformLocation(s->ShaderIds[0], "ProjectionMatrix");
    s->ResolutionUniformLocation = glGetUniformLocation(s->ShaderIds[0], "resolution");
    s->FovUniformLocation = glGetUniformLocation(s->ShaderIds[0], "fov");
    s->YawUniformLocation = glGetUniformLocation(s->ShaderIds[0], "yaw");
    s->PitchUniformLocation = glGetUniformLocation(s->ShaderIds[0], "pitch");
    s->RollUniformLocation = glGetUniformLocation(s->ShaderIds[0], "roll");

    ExitOnGLError(ctx, "ERROR: Could not get the shader uniform locations");

    // BufferIds[3]: VAO, VBO1 (pos), VBO2 (uv)
    glGenBuffers(3, &s->BufferIds[1]);
    ExitOnGLError(ctx, "ERROR: Could not generate the buffer objects");

    glGenVertexArrays(1, &s->BufferIds[0]);
    ExitOnGLError(ctx, "ERROR: Could not generate the VAO");
    glBindVertexArray(s->BufferIds[0]);
    ExitOnGLError(ctx, "ERROR: Could not bind the VAO");

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    ExitOnGLError(ctx, "ERROR: Could not enable vertex attributes");

    glBindBuffer(GL_ARRAY_BUFFER, s->BufferIds[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6 * s->layout->nr, s->vertices, GL_STATIC_DRAW);
    ExitOnGLError(ctx, "ERROR: Could not bind the VBO to the VAO");

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)(sizeof(s->vertices[0].position)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)(sizeof(s->vertices[0].position) + sizeof(s->vertices[0].uv)));
    ExitOnGLError(ctx, "ERROR: Could not set VAO attribute");

    glBindVertexArray(0);

    return 0;
}

int DrawTiles(AVFilterContext *ctx, double rotations[3], const GLfloat res[2])
{
    ProjectContext *s = ctx->priv;
    static int count = 0;

    /* s->ProjectionMatrix = CreateProjectionMatrix((float)(s->vfov), (s->h * 1.0f / s->w), .1f, 5.0f); */
    s->ProjectionMatrix = CreateProjectionMatrix(s->fovx, s->fovy, .5f, 2.0f);

    s->ModelMatrix = IDENTITY_MATRIX;

    RotateAboutY(&s->ModelMatrix, DegreesToRadians(rotations[1]));
    RotateAboutX(&s->ModelMatrix, DegreesToRadians(rotations[0]));
    RotateAboutZ(&s->ModelMatrix, DegreesToRadians(rotations[2]));


    s->ViewMatrix = IDENTITY_MATRIX;
    // TranslateMatrix(&s->ViewMatrix, 0, 0, 1.0);

    glUseProgram(s->ShaderIds[0]);
    if(CheckGLError(ctx, "ERROR: Could not use the shader program"))
        return ENOSYS;

    glUniformMatrix4fv(s->ModelMatrixUniformLocation, 1, GL_FALSE, s->ModelMatrix.m);
    glUniformMatrix4fv(s->ViewMatrixUniformLocation, 1, GL_FALSE, s->ViewMatrix.m);
    glUniformMatrix4fv(s->ProjectionMatrixUniformLocation, 1, GL_FALSE, s->ProjectionMatrix.m);
    /* glUniformMatrix4fv(s->ProjectionMatrixUniformLocation, 1, GL_FALSE, IDENTITY_MATRIX.m); */

    glUniform2fv(s->ResolutionUniformLocation, 1, res);
    glUniform1f(s->FovUniformLocation, s->fovx);
    glUniform1f(s->YawUniformLocation, rotations[1]);
    glUniform1f(s->PitchUniformLocation, rotations[0]);
    glUniform1f(s->RollUniformLocation, rotations[2]);

    if(CheckGLError(ctx, "ERROR: Could not set the shader uniforms"))
        return ENOSYS;

    glBindVertexArray(s->BufferIds[0]);
    if(CheckGLError(ctx, "ERROR: Could not bind the VAO for drawing purpose"))
        return ENOSYS;

    glBindVertexArray(s->BufferIds[0]);
    if(CheckGLError(ctx, "ERROR: Could not bind the VAO"))
        return ENOSYS;

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    if(CheckGLError(ctx, "ERROR: Could not enable vertex attributes"))
        return ENOSYS;

    glBindBuffer(GL_ARRAY_BUFFER, s->BufferIds[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 6 * s->layout->nr, s->vertices, GL_STATIC_DRAW);
    if(CheckGLError(ctx, "ERROR: Could not bind the VBO to the VAO"))
        return ENOSYS;

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)(sizeof(s->vertices[0].position)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(s->vertices[0]), (GLvoid*)(sizeof(s->vertices[0].position) + sizeof(s->vertices[0].uv)));
    if(CheckGLError(ctx, "ERROR: Could not set VAO attribute"))
        return ENOSYS;

    glDrawArrays(GL_TRIANGLES, 0, s->layout->nr * 6);

    if(CheckGLError(ctx, "ERROR: Could not draw the tiles"))
        return ENOSYS;

    glBindVertexArray(0);
    glUseProgram(0);

    count++;
    return 0;
}

void DestroyCube(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    if(s->WindowHandle >= 0)
        glfwMakeContextCurrent (s->WindowHandle);

    //av_log(ctx, AV_LOG_INFO, "[OpenGL] INFO: program id %d, fragshader id %d, vertexshader id %d\n", s->ShaderIds[0], s->ShaderIds[1], s->ShaderIds[2]);

    if(s->ShaderIds[1]){
        glDetachShader(s->ShaderIds[0], s->ShaderIds[1]);
        ExitOnGLError(ctx, "ERROR: Could not detach shader 1");
        glDeleteShader(s->ShaderIds[1]);
    }
    if(s->ShaderIds[2]){
        glDetachShader(s->ShaderIds[0], s->ShaderIds[2]);
        ExitOnGLError(ctx, "ERROR: Could not detach shader 2");
        glDeleteShader(s->ShaderIds[2]);
    }

    if(s->ShaderIds[0]){
        glDeleteProgram(s->ShaderIds[0]);
        ExitOnGLError(ctx, "ERROR: Could not destroy the program objects");
    }

    if(s->BufferIds[1]){
        glDeleteBuffers(2, &s->BufferIds[1]);
        ExitOnGLError(ctx, "ERROR: Could not destroy the buffer objects");
    }

    if(s->BufferIds[0]){
        glDeleteVertexArrays(1, &s->BufferIds[0]);
        ExitOnGLError(ctx, "ERROR: Could not destroy the buffer objects");
    }
}

int CreateTexutre(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    glGenTextures(1, &s->TextureId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->TextureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if(CheckGLError(ctx, "ERROR: Could not setup texture parameter"))
        return ENOSYS;

    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

// May need to do some work on checking pixel format
void LoadTexture(AVFilterContext *ctx, int w, int h, uint8_t *img)
{
    ProjectContext *s = ctx->priv;

    glBindTexture(GL_TEXTURE_2D, s->TextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, img);
    ExitOnGLError(ctx, "ERROR: Could not load image to texture");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ExitOnGLError(ctx, "ERROR: Could not setup texture parameter");

    glBindTexture(GL_TEXTURE_2D, 0);
}

void DestroyTexture(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    glDeleteTextures(1, &s->TextureId);
    ExitOnGLError(ctx, "ERROR: Could not destroy the texture");
}

void CreateFramebuffer(AVFilterContext *ctx, int w, int h)
{
    ProjectContext *s = ctx->priv;

    /* glGenFramebuffers(1, &s->FramebufferId); */
    /* glGenRenderbuffers(1, &s->RenderbufferId); */

    glBindRenderbuffer(GL_RENDERBUFFER, s->RenderbufferId);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_R8, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, s->FramebufferId);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->RenderbufferId);
    ExitOnGLError(ctx, "ERROR: Could not generate frame buffer and render buffer");

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ExitOnGLError(ctx, "ERROR: Could not draw to buffer color attachment 0");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CreateFramebuffer2(AVFilterContext *ctx, int w, int h)
{
    ProjectContext *s = ctx->priv;

    /* glGenFramebuffers(1, &s->FramebufferId2); */
    /* glGenRenderbuffers(1, &s->RenderbufferId2); */

    glBindRenderbuffer(GL_RENDERBUFFER, s->RenderbufferId2);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_R8, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, s->FramebufferId2);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->RenderbufferId2);
    ExitOnGLError(ctx, "ERROR: Could not generate frame buffer and render buffer");

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ExitOnGLError(ctx, "ERROR: Could not draw to buffer color attachment 0");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DestroyFramebuffer(AVFilterContext *ctx)
{
    ProjectContext *s = ctx->priv;

    glDeleteRenderbuffers(1, &s->RenderbufferId);
    glDeleteFramebuffers(1, &s->FramebufferId);
    ExitOnGLError(ctx, "ERROR: Could not destroy render buffer and frame buffer");
}

void printPixelFormat(AVFilterContext *ctx, const AVPixFmtDescriptor *desc)
{
    uint64_t flags = desc->flags;
    av_log(ctx, AV_LOG_INFO, "[Project Filter] Pixel format %s: ", desc->name);
    if(flags & AV_PIX_FMT_FLAG_BE)
        av_log(ctx, AV_LOG_INFO, "Big Endian, ");
    if(flags & AV_PIX_FMT_FLAG_PAL)
        av_log(ctx, AV_LOG_INFO, "Palette data, ");
    if(flags & AV_PIX_FMT_FLAG_BITSTREAM)
        av_log(ctx, AV_LOG_INFO, "Bit-wise packed, ");
    if(flags & AV_PIX_FMT_FLAG_HWACCEL)
        av_log(ctx, AV_LOG_INFO, "HW accelerated format, ");
    if(flags & AV_PIX_FMT_FLAG_PLANAR)
        av_log(ctx, AV_LOG_INFO, "Plannar pixel format, ");
    if(flags & AV_PIX_FMT_FLAG_RGB)
        av_log(ctx, AV_LOG_INFO, "RGB-like (as opposed to YUV/grayscale), ");
    if(flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        av_log(ctx, AV_LOG_INFO, "Pseudo-paletted data, ");
    if(flags & AV_PIX_FMT_FLAG_ALPHA)
        av_log(ctx, AV_LOG_INFO, "Alpha data, ");
    if(flags & AV_PIX_FMT_FLAG_BAYER)
        av_log(ctx, AV_LOG_INFO, "Bayer pattern, ");
    av_log(ctx, AV_LOG_INFO, "\n");
}

void write_png_file(char *filename, int w, int h, uint8_t *d)
{
    int i;
    FILE *fp;
    png_structp png;
    png_infop info;

    png_byte **r_ptrs;
    r_ptrs = malloc(sizeof(png_byte *) * h);
    for(i = 0; i < h; i++){
        r_ptrs[i] = malloc(sizeof(png_byte) * w * 4);
        memcpy(r_ptrs[i], d + i*w*4, w*4);
    }

    fp = fopen(filename, "wb");

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    info = png_create_info_struct(png);

//    if(setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);

    png_set_IHDR(
        png,
        info,
        w,h,
        8,
        PNG_COLOR_TYPE_RGB_ALPHA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, r_ptrs);
    png_write_end(png, NULL);

    for(i = 0; i < h; i++)
        free(r_ptrs[i]);
    free(r_ptrs);

    fclose(fp);
}
