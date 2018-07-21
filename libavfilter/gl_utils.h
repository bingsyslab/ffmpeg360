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

#ifndef GLFW_TRUE
#define GLFW_TRUE GL_TRUE
#endif
#ifndef GLFW_FALSE
#define GLFW_FALSE GL_FALSE
#endif

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



#endif
