#include "texture.h"
#include "test_util.h"

#include "../renderer.h"
#include "../shader.h"
#include "../input.h"

namespace {

const int TEX_WIDTH = 64;
const int TEX_HEIGHT = 64;

byte checkerboard[ TEX_WIDTH ][ TEX_HEIGHT ][ 4 ];

GLuint vao, vbo, texture;

GLuint program;

glm::mat4 cubeModel = glm::scale( glm::mat4( 1.0f ), glm::vec3( 10.0f ) );

const glm::mat4& testRotMatrix = glm::rotate( glm::mat4( 1.0f ), glm::radians( 1.0f ), glm::vec3( 1.0f, 1.0f, 0.0f ) );

InputCamera camera;

bool cursorVisible;

}

void TEX_HandleKeyInput( GLFWwindow* w, int key, int scancode, int action, int mods )
{
    OnKeyPress( w, key, scancode, action, mods, camera, cursorVisible );
}

void TEX_HandleMouseMove( GLFWwindow* w, double x, double y )
{
    camera.EvalMouseMove( ( float ) x, ( float ) y );
}

void TEX_LoadTest( GLFWwindow* window )
{
    glfwSetKeyCallback( window, TEX_HandleKeyInput );
    glfwSetCursorPosCallback( window, TEX_HandleMouseMove );
    glfwSetInputMode( window, GLFW_CURSOR, GLFW_CURSOR_NORMAL );

    cursorVisible = true;

    // Generate our texture colors
    for ( int i = 0; i < TEX_WIDTH; ++i )
    {
        for ( int j = 0; j < TEX_HEIGHT; ++j )
        {
            unsigned char v = ( ( ( ( i & 0x8 ) == 0 ) ^ ( ( j & 0x8 ) == 0 ) ) * 255 ) & 128;

            checkerboard[ i ][ j ][ 0 ] = v;
            checkerboard[ i ][ j ][ 1 ] = v;
            checkerboard[ i ][ j ][ 2 ] = v;
            checkerboard[ i ][ j ][ 3 ] = v;
        }
    }

    GLfloat vertexData[] =
    {
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 1.0f, 0.0f,

         0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,

        -1.0f, -1.0f, -0.5f, 0.0f, 0.0f,
         1.0f, -1.0f, -0.5f, 1.0f, 0.0f,
         1.0f,  1.0f, -0.5f, 1.0f, 1.0f,
         1.0f,  1.0f, -0.5f, 1.0f, 1.0f,
        -1.0f,  1.0f, -0.5f, 0.0f, 1.0f,
        -1.0f, -1.0f, -0.5f, 0.0f, 0.0f
    };

    program = []( void ) -> GLuint
    {
        GLuint shaders[] =
        {
            CompileShader( "src/tex2D.vert", GL_VERTEX_SHADER ),
            CompileShader( "src/tex2D.frag", GL_FRAGMENT_SHADER )
        };

        return LinkProgram( shaders, 2 );
    }();

    glBindAttribLocation( program, 0, "position" );
    glBindAttribLocation( program, 1, "uv" );

    glGenVertexArrays( 1, &vao );
    glBindVertexArray( vao );

    glGenBuffers( 1, &vbo );
    glBindBuffer( GL_ARRAY_BUFFER, vbo );
    glBufferData( GL_ARRAY_BUFFER, sizeof( vertexData ), vertexData, GL_STATIC_DRAW );

    glEnableVertexAttribArray( 0 );
    glEnableVertexAttribArray( 1 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 5, 0 );
    glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( float ) * 5, ( void* )( sizeof( float ) * 3 ) );

    glBindVertexArray( 0 );

    glGenTextures( 1, &texture);
    glBindTexture( GL_TEXTURE_2D, texture );

    glTexStorage2D( GL_TEXTURE_2D, 4, GL_RGBA8, TEX_WIDTH, TEX_HEIGHT );
    glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, TEX_WIDTH, TEX_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, ( void* )checkerboard );

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

    glBindTexture( GL_TEXTURE_2D, 0 );

    glUseProgram( program );
    glUniformMatrix4fv( glGetUniformLocation( program, "projection" ), 1, GL_FALSE, glm::value_ptr( camera.ViewData().projection ) );
    glUseProgram( 0 );
}

void TEX_DrawTest( void )
{
    glUseProgram( program );
    glBindVertexArray( vao );
    glBindBuffer( GL_ARRAY_BUFFER, vbo );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, texture );

    glUniform1i( glGetUniformLocation( program, "sampler" ), 0 );

    glUniformMatrix4fv( glGetUniformLocation( program, "model" ), 1, GL_FALSE, glm::value_ptr( cubeModel ) );
    glUniformMatrix4fv( glGetUniformLocation( program, "view" ), 1, GL_FALSE, glm::value_ptr( camera.ViewData().transform ) );

    glDrawArrays( GL_TRIANGLES, 0, 36 );

    glBindTexture( GL_TEXTURE_2D, 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    glBindVertexArray( 0 );
    glUseProgram( 0 );

    cubeModel *= testRotMatrix;

    camera.Update();
}

