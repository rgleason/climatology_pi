#include "gldefs.h"                 // MUST be first
#include "climatology_shaders.h"
#include <cstdio>
#include "linmath.h"
#include <wx/log.h>


GLuint pi_climatology_blend_shader_program = 0;
GLuint pi_climatology_blend_vertex_shader = 0;
GLuint pi_climatology_blend_fragment_shader = 0;

static const char* climatology_blend_vertex_shader_source = R"(
attribute vec2 aPos;
attribute vec2 aUV;

uniform mat4 MVMatrix;
uniform mat4 TransformMatrix;

varying vec2 vUV;

void main() {
    gl_Position = MVMatrix * TransformMatrix * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* climatology_blend_fragment_shader_source = R"(
precision mediump float;

uniform sampler2D uTex1;
uniform sampler2D uTex2;

uniform float blendFactor;
uniform vec4 color;

varying vec2 vUV;

void main() {
    vec4 c1 = texture2D(uTex1, vUV);
    vec4 c2 = texture2D(uTex2, vUV);

    vec4 blended = mix(c1, c2, blendFactor);

    gl_FragColor = blended + color;
}
)";

bool LoadClimatologyShaders() {
	if (!glCreateShader || !glShaderSource || !glCompileShader || !glLinkProgram) {
        wxLogMessage("climatology_pi: GL functions not available yet, deferring shader load.");
        return false;
    }
    GLint success;
    char infoLog[512];

    // Vertex shader
    pi_climatology_blend_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(pi_climatology_blend_vertex_shader, 1,
                   &climatology_blend_vertex_shader_source, NULL);
    glCompileShader(pi_climatology_blend_vertex_shader);
    glGetShaderiv(pi_climatology_blend_vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(pi_climatology_blend_vertex_shader, 512, NULL, infoLog);
        printf("Climatology vertex shader error:\n%s\n", infoLog);
        return false;
    }

    // Fragment shader
    pi_climatology_blend_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(pi_climatology_blend_fragment_shader, 1,
                   &climatology_blend_fragment_shader_source, NULL);
    glCompileShader(pi_climatology_blend_fragment_shader);
    glGetShaderiv(pi_climatology_blend_fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(pi_climatology_blend_fragment_shader, 512, NULL, infoLog);
        printf("Climatology fragment shader error:\n%s\n", infoLog);
        return false;
    }

    // Program
    pi_climatology_blend_shader_program = glCreateProgram();
    glAttachShader(pi_climatology_blend_shader_program, pi_climatology_blend_vertex_shader);
    glAttachShader(pi_climatology_blend_shader_program, pi_climatology_blend_fragment_shader);
    glLinkProgram(pi_climatology_blend_shader_program);
    glGetProgramiv(pi_climatology_blend_shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(pi_climatology_blend_shader_program, 512, NULL, infoLog);
        printf("Climatology shader program link error:\n%s\n", infoLog);
        return false;
    }
	// NEW SAFETY CHECK
	if (!pi_climatology_blend_shader_program) {
		printf("Climatology shader program is 0 ? invalid program object.\n");
		return false;
	}
    return true;
}

void ConfigureClimatologyShader(float width, float height) {
    // Safety check: ensure shader program is valid
    if (!pi_climatology_blend_shader_program) {
        wxLogWarning("ConfigureClimatologyShader: shader program not loaded yet");
        return;
    }

    float vp_transform[16];
    mat4x4 m;
    mat4x4_identity(m);
    mat4x4_scale_aniso((float(*)[4])vp_transform, m, 2.0 / width, -2.0 / height, 1.0);
    mat4x4_translate_in_place((float(*)[4])vp_transform, -width / 2, -height / 2, 0);

    mat4x4 I;
    mat4x4_identity(I);

    glUseProgram(pi_climatology_blend_shader_program);

    GLint mvLoc = glGetUniformLocation(pi_climatology_blend_shader_program, "MVMatrix");
    glUniformMatrix4fv(mvLoc, 1, GL_FALSE, (const GLfloat*)vp_transform);

    GLint tLoc = glGetUniformLocation(pi_climatology_blend_shader_program, "TransformMatrix");
    glUniformMatrix4fv(tLoc, 1, GL_FALSE, (const GLfloat*)I);
}
