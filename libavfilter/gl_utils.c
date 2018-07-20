#include "gl_utils.h"

const Matrix IDENTITY_MATRIX = {
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    }
};

float Cotangent(float angle)
{
    return (float)(1.0 / tan(angle));
}

float DegreesToRadians(float degrees)
{
    return degrees*(float)(PI / 180);
}

float RadiansToDegrees(float radians)
{
    return radians*(float)(180 / PI);
}

Matrix MultiplyMatrices(const Matrix *m1, const Matrix *m2)
{
    Matrix out = IDENTITY_MATRIX;
    unsigned int row, column, row_offset;

    for(row = 0, row_offset = row * 4; row < 4; ++row, row_offset = row * 4){
        for(column = 0; column < 4; ++column){
            out.m[row_offset + column] =
                ( m1->m[row_offset + 0] * m2->m[column + 0] ) +
                ( m1->m[row_offset + 1] * m2->m[column + 4] ) +
                ( m1->m[row_offset + 2] * m2->m[column + 8] ) +
                ( m1->m[row_offset + 3] * m2->m[column + 12]);
        }
    }

    return out;
}

void MultiplyVertex(const Matrix *m1, Vertex *v)
{
    unsigned int row, col;
    float pos[4];
    for(row = 0; row < 4; row++){
        pos[row] = 0;
        for(col = 0; col < 4; col++)
            pos[row] += ( m1->m[row * 4 + col] * v->position[col] );
    }

    for(col = 0; col < 4; col++)
        v->position[col] = pos[col];
}

void ScaleMatrix(Matrix *m, float x, float y, float z)
{
    Matrix pdt, scale = IDENTITY_MATRIX;

    scale.m[0] = x;
    scale.m[5] = y;
    scale.m[10] = z;

    pdt = MultiplyMatrices(m, &scale);
    memcpy(m->m, pdt.m, sizeof(m->m));
}

void TranslateMatrix(Matrix *m, float x, float y, float z)
{
    Matrix pdt, translation = IDENTITY_MATRIX;
    translation.m[12] = x;
    translation.m[13] = y;
    translation.m[14] = z;

    pdt= MultiplyMatrices(m, &translation);
    memcpy(m->m, pdt.m, sizeof(m->m));
}

void RotateAboutX(Matrix *m, float angle)
{
    Matrix pdt, rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[5] = cosine;
    rotation.m[6] = sine;
    rotation.m[9] = -sine;
    rotation.m[10] = cosine;

    pdt = MultiplyMatrices(m, &rotation);
    memcpy(m->m, pdt.m, sizeof(m->m));
}

void RotateAboutY(Matrix *m, float angle)
{
    Matrix pdt, rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[0] = cosine;
    rotation.m[8] = sine;
    rotation.m[2] = -sine;
    rotation.m[10] = cosine;

    pdt = MultiplyMatrices(m, &rotation);
    memcpy(m->m, pdt.m, sizeof(m->m));
}

void RotateAboutZ(Matrix *m, float angle)
{
    Matrix pdt, rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[0] = cosine;
    rotation.m[1] = sine;
    rotation.m[4] = -sine;
    rotation.m[5] = cosine;

    pdt = MultiplyMatrices(m, &rotation);
    memcpy(m->m, pdt.m, sizeof(m->m));
}

Matrix CreateProjectionMatrix(float fovx, float fovy, float near_plane, float far_plane)
{
    Matrix out = { {0} };

    const float y_scale = Cotangent(DegreesToRadians(fovy / 2.0));
    const float x_scale = Cotangent(DegreesToRadians(fovx / 2.0));
    const float frustum_length = far_plane - near_plane;

    out.m[0] = x_scale;
    out.m[5] = y_scale;
    out.m[10] = -((far_plane + near_plane) / frustum_length);
    out.m[11] = -1;
    out.m[14] = -((2* near_plane * far_plane) / frustum_length);

    return out;
}

void ExitOnGLError(void *avctx, const char *error_message)
{
    const GLenum ErrorValue = glGetError();

    if(ErrorValue != GL_NO_ERROR){
        av_log(avctx, AV_LOG_ERROR, "[OpenGL] %s: %s\n", error_message, gluErrorString(ErrorValue));
        exit(EXIT_FAILURE);
    }
}

int CheckGLError(void *avctx, const char *error_message)
{
    const GLenum ErrorValue = glGetError();

    if(ErrorValue != GL_NO_ERROR){
        av_log(avctx, AV_LOG_ERROR, "[OpenGL] %s: %s\n", error_message, gluErrorString(ErrorValue));
        return -1;
    }else{
        return 0;
    }
}

GLuint LoadShader(void *avctx, const char *filename, GLenum shader_type)
{
    GLuint shader_id = 0;
    GLint compRes = 0, logSize = 0;
    GLchar *log;
    FILE *file;
    long file_size = -1;
    char *glsl_source;

    av_log(avctx, AV_LOG_INFO, "[OpenGL] Try loading shader file %s... \n", filename);

    const char* shader_dir = "ffmpeg360_shader/";
    const size_t shader_path_length = strlen(shader_dir) + strlen(filename) + 1;
    char* shader_path = malloc(shader_path_length);

    snprintf(shader_path, shader_path_length, "%s%s", shader_dir, filename);

    if(NULL != (file = fopen(shader_path, "rb")) &&
       0 == fseek(file, 0, SEEK_END) &&
       -1 != (file_size = ftell(file)))
    {
        rewind(file);

        if(NULL != (glsl_source = (char *)malloc(file_size+1))){
            if(file_size == (long)fread(glsl_source, sizeof(char), file_size, file)){
                glsl_source[file_size] = '\0';

                if(0 != (shader_id = glCreateShader(shader_type))){
                    glShaderSource(shader_id, 1, (const GLchar **)(&glsl_source), NULL);
                    glCompileShader(shader_id);
                    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &compRes);
                    if(GL_FALSE == compRes){
                        av_log(avctx, AV_LOG_ERROR, "[OpenGL] compiling %s failed: \n", filename);
                        glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &logSize);
                        log = malloc(logSize * sizeof(GLchar));
                        glGetShaderInfoLog(shader_id, logSize, NULL, log);
                        av_log(avctx, AV_LOG_ERROR, "[OpenGL] \n%s\n", log);
                        free(log);
                    }
                }else
                    av_log(avctx, AV_LOG_ERROR, "[OpenGL] Could not create a shader");
            }else
                av_log(avctx, AV_LOG_ERROR, "[OpenGL] ERROR: Could not read a file");

            free(glsl_source);
        }else
            av_log(avctx, AV_LOG_ERROR, "[OpenGL] ERROR: Could not allocate %ld bytes.\n", file_size);

        fclose(file);
    }else{
        if(NULL != file)
            fclose(file);
        av_log(avctx, AV_LOG_ERROR, "[OpenGL] ERROR: Could not open file %s\n", filename);
    }

    free(shader_path);
    return shader_id;
}
