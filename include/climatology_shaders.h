#pragma once

#include <GL/glew.h>
#include <GL/gl.h>

// Shader program handle
extern GLuint pi_climatology_blend_shader_program;

// Shader loader
bool LoadClimatologyShaders();

// Shader uniform/attribute configuration
void ConfigureClimatologyShader(float width, float height);