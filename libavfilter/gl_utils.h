#ifndef _M_GL_UTILS_H
#define _M_GL_UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <GL/glew.h>
//#include <GL/freeglut.h>
#include <GLFW/glfw3.h>
#include "libavutil/log.h"

#include <stdint.h>
#include <stdlib.h>


static const double PI = 3.14159265358979323846;

typedef struct Vertex {
    float position[4];
    float uv[2]; // absolute coordinates on the frame
    float uvr[4]; // local coordinates, w and h
} Vertex;

typedef struct Matrix {
    float m[16];
} Matrix;

extern const Matrix IDENTITY_MATRIX;

float Cotangent(float angle);
float DegreesToRadians(float degrees);
float RadiansToDegrees(float radians);

Matrix MultiplyMatrices(const Matrix* m1, const Matrix *m2);
void MultiplyVertex(const Matrix *m1, Vertex *v);

void RotateAboutX(Matrix *m, float angle);
void RotateAboutY(Matrix *m, float angle);
void RotateAboutZ(Matrix *m, float angle);
void ScaleMatrix(Matrix *m, float x, float y, float z);
void TranslateMatrix(Matrix *m, float x, float y, float z);

Matrix CreateProjectionMatrix(float fovx, float fovy, float near_plane, float far_plane);

void ExitOnGLError(void *avctx, const char *error_message);
int CheckGLError(void *avctx, const char *error_message);
GLuint LoadShader(void *avctx, const char* filename, GLenum shader_type);



#define ITEM_STR_LEN 128
#define ENLARGE_ITEM_NR 20

typedef union {
    char str[ITEM_STR_LEN];

    uint64_t u64;

    uint32_t u32;

    int64_t  i64;

    int32_t  i32;

    void *ptr;

} vector_item_t;

typedef struct {
    vector_item_t *head;

    uint64_t nr;

    uint64_t size;
} vector_t;

static vector_t *init_vector(void)
{
    vector_t *v = (vector_t *)malloc(sizeof(vector_t));
    v->head = NULL;
    v->nr = 0;
    v->size = 0;
    return v;
}

static void destroy_vector(vector_t *v)
{
    if(v->size)
        free(v->head);
    free(v);
}

static void *enlarge_vector(vector_t *v)
{
    v->size += ENLARGE_ITEM_NR;
    v->head = realloc(v->head, sizeof(vector_item_t) * v->size);
    return v;
}

static void push_back(vector_t *v, vector_item_t item)
{
    if(v->nr >= v->size)
        enlarge_vector(v);
    memcpy(&v->head[v->nr], &item, sizeof(vector_item_t));
    /* v->head[v->nr] = item; */
    v->nr++;
}

/* static void pr_items_str(vector_t *v) */
/* { */
/*     int i; */
/*     printf("items (%lu/%lu):\n", v->nr, v->size); */
/*     for(i = 0; i < v->nr; i++){ */
/*         printf("  %s\n", v->head[i].str); */
/*     } */
/*     printf("\n"); */
/* } */

#endif
