#version 330 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex0;
layout(location = 2) in vec2 lightmap;
layout(location = 3) in vec4 color;

uniform mat4 modelToView;
uniform mat4 viewToClip;

smooth out vec4 frag_Color;
smooth out vec2 frag_Tex;
smooth out vec2 frag_Lightmap;

void main()
{
    gl_Position = viewToClip * modelToView * vec4( position, 1.0 );

    frag_Color = color;
    frag_Tex = tex0;
    frag_Lightmap = lightmap;
}