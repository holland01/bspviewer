#include "trenderer.h"
#include "test_util.h"
#include "../input.h"

namespace {

double currTime, prevTime;

bool cursorVisible;

}

BSPRenderer* renderer = NULL;

void BSPR_HandleKeyInput( GLFWwindow* w, int key, int scancode, int action, int mods )
{
    OnKeyPress( w, key, scancode, action, mods, renderer->camera, cursorVisible );
}

void BSPR_HandleMouseMove( GLFWwindow* w, double x, double y )
{
    renderer->camera.EvalMouseMove( ( float ) x, ( float ) y );
}

void BSPR_LoadTest( GLFWwindow* window )
{
    prevTime = glfwGetTime();

    glfwSetKeyCallback( window, BSPR_HandleKeyInput );
    glfwSetCursorPosCallback( window, BSPR_HandleMouseMove );
    glfwSetInputMode( window, GLFW_STICKY_KEYS, GL_FALSE );

    renderer = new BSPRenderer;
    renderer->Prep();
    renderer->Load( "asset/quake/aty3dm1v2/aty3dm1v2/maps/aty3dm1v2.bsp" );

    cursorVisible = true;

}

void BSPR_DrawTest( void )
{
    currTime = glfwGetTime();

    renderer->Update( currTime - prevTime );
    renderer->DrawWorld();

    prevTime = currTime;
}