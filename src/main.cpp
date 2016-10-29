#include "common.h"
#include "renderer.h"
#include "io.h"
#include "tests/trenderer.h"
#include "tests/test_textures.h"
#include "tests/testiowebworker.h"
#include "tests/test_atlas_struct.h"
#include "renderer/buffer.h"
#include <iostream>

#undef main

#ifdef EMSCRIPTEN
#	include <emscripten.h>
#endif

// Is global
void FlagExit( void )
{
	if ( gAppTest )
	{
		delete gAppTest;
		gAppTest = nullptr;
	}
#ifdef EMSCRIPTEN
	//emscripten_force_exit( 0 );
#else
	exit( 0 );
#endif
}

#define SIZE_ERROR_MESSAGE "Unsupported type size found."

/*
#ifdef EMSCRIPTEN
#	define IOTEST
#endif
*/

static INLINE std::string FullPath( const char* path, size_t pathLen )
{
	const char* croot = "/working";
	std::string root( croot );

	std::string strPath = "/";
	strPath.append( path, pathLen );
	root.append( strPath );

	//printf( "Path Received: %s\n", root.c_str() );

	return root;
}

static void TryAppend( const char* base )
{
	std::string b( FullPath( base, strlen( base ) + 1 ) );
	std::string ext( ".jpg" );

	b.append( ext );

	std::cout << "New Path: " << b << std::endl;
}

int main( void )
{
	static_assert( sizeof( glm::vec3 ) == sizeof( float ) * 3, SIZE_ERROR_MESSAGE );
	static_assert( sizeof( glm::vec2 ) == sizeof( float ) * 2, SIZE_ERROR_MESSAGE );
	static_assert( sizeof( glm::ivec3 ) == sizeof( int ) * 3, SIZE_ERROR_MESSAGE );

	TryAppend( "asset/models/mapobjects/Skull/skull" );

	//std::string testPath("/working/asset/textures/gothic_door/"
		//"door02_i_ornate5_fin");

	//std::string accum( ".jpg" );

	//testPath += accum;

	//std::cout << "New Path: " << testPath << std::endl;

#if defined( EMSCRIPTEN ) && defined( IOTEST )
	IOTestWebWorker test;
	return test();
#else
	gAppTest = new TRenderer( ASSET_Q3_ROOT"/maps/q3dm2.bsp" );
	gAppTest->Load();
#endif
}
