#include "core/Core.h"
#include "network/Network.h"
#include "CommandLine.h"
#include "Global.h"
#include <time.h>
#include <stdlib.h>

#ifdef CLIENT

// ===================================================================================================================
//                                                       CLIENT
// ===================================================================================================================

static const bool fullscreen = true;

#include "Font.h"
#include "Render.h"
#include "GameClient.h"
#include "ShaderManager.h"
#include "FontManager.h"
#include "MeshManager.h"
#include "StoneManager.h"
#include "InputManager.h"
#include "DemoManager.h"
#include "Console.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

CONSOLE_FUNCTION( quit )
{
    global.quit = true;    
}

CONSOLE_FUNCTION( reload )
{
    global.fontManager->Reload();
    global.shaderManager->Reload();
    global.meshManager->Reload();
    global.stoneManager->Reload();  

    // todo: maybe inform demo manager that stuff was reloaded
}

static void game_init()
{
    auto & allocator = core::memory::default_allocator();

    global.console = CORE_NEW( allocator, Console, allocator );

    global.fontManager = CORE_NEW( allocator, FontManager, allocator );
    global.shaderManager = CORE_NEW( allocator, ShaderManager, allocator );
    global.meshManager = CORE_NEW( allocator, MeshManager, allocator );
    global.stoneManager = CORE_NEW( allocator, StoneManager, allocator );
    global.inputManager = CORE_NEW( allocator, InputManager, allocator );
    global.demoManager = CORE_NEW( allocator, DemoManager, allocator );

    global.client = CreateGameClient( core::memory::default_allocator() );

    if ( !global.client )
    {
        printf( "%.3f: error: failed to create game client!\n", global.timeBase.time );
        exit( 1 );
    }

    global.timeBase.deltaTime = 1.0 / TickRate;

    glEnable( GL_FRAMEBUFFER_SRGB );

    glEnable( GL_CULL_FACE );
    glFrontFace( GL_CW );

    check_opengl_error( "after game_init" );
}

static void game_update()
{
    global.client->Update( global.timeBase );

    Demo * demo = global.demoManager->GetDemo();
    if ( demo )
        demo->Update();

    global.timeBase.time += global.timeBase.deltaTime;
}

static double start_time = 0.0;
static int current_fps = 60;
static int frame_count = 0;
static int initial_wait = 120;

static void update_fps()
{
    if ( initial_wait-- > 0 )
        return;

    if ( frame_count == 0 )
    {
        start_time = core::time();
    }

    frame_count++;

    const int sample_frames = 20;

    if ( frame_count == sample_frames )
    {
        double finish_time = core::time();
        double delta_time = ( finish_time - start_time ) / sample_frames;
        current_fps = (int) floor( ( 1.0 / delta_time ) + 0.001 ); 
        const int display_refresh_fps = 60; // todo: ideally get this from the OS
        if ( current_fps > display_refresh_fps )
            current_fps = display_refresh_fps;
        frame_count = 0;
    }
}

static void render_fps()
{
    if ( current_fps == 0 )
        return;

    char fps_string[256];
    snprintf( fps_string, (int) sizeof( fps_string ), "%3d   ", current_fps );

    Font * font = global.fontManager->GetFont( "FPS" );
    if ( font )
    {
        const float text_x = global.displayWidth - font->GetTextWidth( fps_string ) - 5;
        const float text_y = 5;

        const Color bad_fps_color = Color(0.6f,0,0);              // red
        const Color good_fps_color = Color( 0.27f,0.81f,1.0f);    // blue
        
        const Color fps_color = ( current_fps >= 55 ) ? good_fps_color : bad_fps_color;

        font->Begin();
        font->DrawText( text_x, text_y, fps_string, fps_color );
        font->DrawText( text_x, text_y, "   FPS", Color(0,0,0) );
        font->End();
    }
}

static void render_scene()
{
    Demo * demo = global.demoManager->GetDemo();
    if ( demo )
        demo->Render();
}

static void render_ui()
{
    // ...
}

static void render_debug()
{
    render_fps();
}

static void render_console()
{
    global.console->Render();
}

static void game_render()
{
    check_opengl_error( "before render" );

    glClearColor( 0.25, 0.25, 0.25, 0.0 );

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glEnable( GL_DEPTH_TEST );

    render_scene();

    glDisable( GL_DEPTH_TEST );

    render_ui();

    render_debug();

    render_console();

    check_opengl_error( "after render" );
}

static void game_shutdown()
{
    auto & allocator = core::memory::default_allocator();

    DestroyGameClient( allocator, global.client );

    CORE_DELETE( allocator, FontManager, global.fontManager );
    CORE_DELETE( allocator, ShaderManager, global.shaderManager );
    CORE_DELETE( allocator, MeshManager, global.meshManager );
    CORE_DELETE( allocator, StoneManager, global.stoneManager );
    CORE_DELETE( allocator, InputManager, global.inputManager );
    CORE_DELETE( allocator, DemoManager, global.demoManager );

    CORE_DELETE( allocator, Console, global.console );

    global = Global();
}

void framebuffer_size_callback( GLFWwindow * window, int width, int height )
{
    global.displayWidth = width;
    global.displayHeight = height;

    glViewport( 0, 0, width, height );
}


void key_callback( GLFWwindow * window, int key, int scancode, int action, int mods )
{
    global.inputManager->KeyEvent( key, scancode, action, mods );
}

void char_callback( GLFWwindow * window, unsigned int code )
{
    global.inputManager->CharEvent( code );
}

int main( int argc, char * argv[] )
{
    srand( time( nullptr ) );

    core::memory::initialize();

    ParseCommandLine( argc, argv );

    if ( !network::InitializeNetwork() )
    {
        printf( "%.3f: Failed to initialize network!\n", global.timeBase.time );
        return 1;
    }

    CORE_ASSERT( network::IsNetworkInitialized() );

    glfwInit();

    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );
    glfwWindowHint( GLFW_SRGB_CAPABLE, GL_TRUE );
    glfwWindowHint( GLFW_RESIZABLE, GL_TRUE );
    glfwWindowHint( GLFW_SAMPLES, 8 );
    glfwWindowHint( GLFW_STENCIL_BITS, 8 );
//    glfwWindowHint( GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE );

    const GLFWvidmode * mode = glfwGetVideoMode( glfwGetPrimaryMonitor() );

    GLFWwindow * window = nullptr;
    if ( fullscreen )
        window = glfwCreateWindow( mode->width, mode->height, "Client", glfwGetPrimaryMonitor(), nullptr );
    else
        window = glfwCreateWindow( 1200, 800, "Client", nullptr, nullptr );

    if ( !window )
    {
        printf( "error: failed to create window\n" );
        exit( 1 );
    }

    glfwGetFramebufferSize( window, &global.displayWidth, &global.displayHeight );

    glfwSetFramebufferSizeCallback( window, framebuffer_size_callback );

    glfwMakeContextCurrent( window );

    glfwSetKeyCallback( window, key_callback );
    glfwSetCharCallback( window, char_callback );

    glewExperimental = GL_TRUE;
    glewInit();

    clear_opengl_error();

    if ( !GLEW_VERSION_4_1 )
    {
        printf( "error: OpenGL 4.1 is not supported :(\n" );
        exit(1);
    }

    game_init();

    CommandLinePostGameInit();

    while ( !global.quit && !glfwWindowShouldClose( window ) )
    {
        update_fps();

        glfwPollEvents();

        game_update();

        glfwPollEvents();

        game_render();

        glfwSwapBuffers( window );
    }

    game_shutdown();

    network::ShutdownNetwork();

    // IMPORTANT: Disabled until I fix leak issue with game client/server objects in config
    //memory::shutdown();

    glfwTerminate();
    
    return 0;
}

// ===================================================================================================================

#else // #ifdef CLIENT

// ===================================================================================================================
//                                                       SERVER
// ===================================================================================================================

#include "GameServer.h"

int main( int argc, char ** argv )
{
    srand( time( nullptr ) );

    global.timeBase.deltaTime = 1.0 / TickRate;

    core::memory::initialize();
    
    if ( !network::InitializeNetwork() )
    {
        printf( "%.3f: Failed to initialize network!\n", global.timeBase.time );
        return 1;
    }

    auto server = CreateGameServer( core::memory::default_allocator(), ServerPort, MaxClients );

    if ( !server )
    {
        printf( "%.3f: Failed to not create server on port %d\n", global.timeBase.time, ServerPort );
        return 1;
    }
    
    printf( "%.3f: Started game server on port %d\n", global.timeBase.time, ServerPort );

    while ( true )
    {
        // ...

        // todo: rather than sleeping for MS we want a signal that comes in every n millis instead
        // so we maintain a steady tick rate. how best to do this on linux, mac and windows respectively?

        // todo: want to detect the CTRL^C signal and actually break outta here

        server->Update( global.timeBase );

        core::sleep_milliseconds( global.timeBase.deltaTime * 1000 );

        global.timeBase.time += global.timeBase.deltaTime;
    }

    printf( "%.3f: Shutting down game server\n", global.timeBase.time );

    DestroyGameServer( core::memory::default_allocator(), server );

    network::ShutdownNetwork();

    core::memory::shutdown();

    return 0;
}

// ===================================================================================================================

#endif // #ifdef CLIENT
