#pragma once

#include "common.h"
#include "bsp_data.h"
#include "gldebug.h"
#include "io.h"
#include <array>

#define UBO_TRANSFORMS_BLOCK_BINDING 0
#define ATTRIB_OFFSET( type, member )( ( void* ) offsetof( type, member ) ) 

// Extensions
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

#if defined( _DEBUG_USE_GL_ASYNC_CALLBACK )
#	define GL_CHECK( expr )\
		do\
		{\
			( expr );\
			glDebugSetCallInfo( std::string( #expr ), _FUNC_NAME_ );\
		}\
		while ( 0 )
#elif defined( _DEBUG_USE_GL_GET_ERR )
#	define GL_CHECK( expr )\
		do\
		{\
			( expr );\
			ExitOnGLError( _LINE_NUM_, #expr, _FUNC_NAME_ );\
		}\
		while ( 0 )
#else
#	define GL_CHECK( expr ) ( expr )
#endif // _DEBUG_USE_GL_ASYNC_CALLBACK

enum 
{
	GLUTIL_POLYGON_OFFSET_FILL = 1 << 0,
	GLUTIL_POLYGON_OFFSET_LINE = 1 << 1,
	GLUTIL_POLYGON_OFFSET_POINT = 1 << 2
};

void SetPolygonOffsetState( bool enable, uint32_t polyFlags );
void ImPrep( const glm::mat4& viewTransform, const glm::mat4& clipTransform );
void ImDrawAxes( const float size );

void LoadVertexLayout( bool mapTexCoords );

static INLINE void MapAttribTexCoord( int location, size_t offset )
{
	GL_CHECK( glEnableVertexAttribArray( location ) );
	GL_CHECK( glVertexAttribPointer( location, 2, GL_FLOAT, GL_FALSE, sizeof( bspVertex_t ), ( void* ) offset ) );
}

static INLINE void MapVec3( int location, size_t offset )
{
	GL_CHECK( glEnableVertexAttribArray( location ) );
	GL_CHECK( glVertexAttribPointer( location, 3, GL_FLOAT, GL_FALSE, sizeof( bspVertex_t ), ( void* ) offset ) );
}

static INLINE void MapUniforms( glHandleMap_t& unifMap, GLuint programID, const std::vector< std::string >& uniforms )
{
	for ( const std::string& title: uniforms )
	{
		GLint uniform;
		GL_CHECK( uniform = glGetUniformLocation( programID, title.c_str() ) );
		unifMap.insert( glHandleMapEntry_t( title, uniform ) );
	}
}

static INLINE void MapProgramToUBO( GLuint programID, const char* uboName )
{
	if ( strcmp( uboName, "Transforms" ) == 0 )
	{
		GLuint uniformBlockLoc;
		GL_CHECK( uniformBlockLoc = glGetUniformBlockIndex( programID, uboName ) );
		GL_CHECK( glUniformBlockBinding( programID, uniformBlockLoc, UBO_TRANSFORMS_BLOCK_BINDING ) );
	}
}

static INLINE GLuint GenVertexArrayObject( void )
{
	GLuint vao;
	GL_CHECK( glGenVertexArrays( 1, &vao ) );
	return vao;
}

template < typename T >
static INLINE GLuint GenBufferObject( GLenum target, const std::vector< T >& data, GLenum usage )
{
	GLuint obj;
	GL_CHECK( glGenBuffers( 1, &obj ) );
	GL_CHECK( glBindBuffer( target, obj ) );
	GL_CHECK( glBufferData( target, data.size() * sizeof( T ), &data[ 0 ], usage ) );
	GL_CHECK( glBindBuffer( target, 0 ) );
	return obj;
}

template < typename T >
static INLINE void UpdateBufferObject( GLenum target, GLuint obj, const std::vector< T >& data ) 
{
	GL_CHECK( glBindBuffer( target, obj ) );
	GL_CHECK( glBufferSubData( target, 0, data.size() * sizeof( T ), &data[ 0 ] ) );
	GL_CHECK( glBindBuffer( target, 0 ) );
}

static INLINE void DeleteBufferObject( GLenum target, GLuint obj )
{
	// Unbind to prevent driver from lazy deletion
	GL_CHECK( glBindBuffer( target, 0 ) );
	GL_CHECK( glDeleteBuffers( 1, &obj ) );
}

static INLINE void LoadBufferLayout( GLuint vbo, bool mapTexCoords )
{
	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, vbo ) );
	LoadVertexLayout( mapTexCoords );
}

static INLINE void DrawElementBuffer( GLuint ibo, size_t numIndices )
{
	GL_CHECK( glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo ) );
	GL_CHECK( glDrawElements( GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, nullptr ) );
	GL_CHECK( glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 ) );
}

struct texture_t
{
	bool srgb: 1;

	GLuint handle;
	GLuint sampler;
	GLenum wrap;
	GLenum format;
	GLenum internalFormat;

	int width, height, bpp; // bpp is in bytes

	std::vector< byte > pixels;

	texture_t( void );
	~texture_t( void );
};

void Tex_SetBufferSize( texture_t& tex, int width, int height, int bpp, byte fill );
void Tex_MakeTexture2D( texture_t& tex );

bool LoadTextureFromFile( const char* texPath, uint32_t loadFlags, texture_t& texture );

// -------------------------------------------------------------------------------------------------
class Program
{
private:
	GLuint program;

	void GenData( const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs );

public:
	std::map< std::string, GLint > uniforms; 
	std::map< std::string, GLint > attribs;

	Program( const std::string& vertexShader, const std::string& fragmentShader );
	
	Program( const std::string& vertexShader, const std::string& fragmentShader, 
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs );
	
	Program( const std::vector< char >& vertexShader, const std::vector< char >& fragmentShader, 
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs );

	~Program( void );

	void AddUnif( const std::string& name );
	void AddAttrib( const std::string& name );

	void LoadMatrix( const std::string& name, const glm::mat4& t ) const;
	void LoadVec3( const std::string& name, const glm::vec3& v ) const;
	void LoadVec4( const std::string& name, const glm::vec4& v ) const;

	void LoadInt( const std::string& name, int v ) const;

	void Bind( void ) const;
	void Release( void ) const;
};

// -------------------------------------------------------------------------------------------------
INLINE void Program::AddUnif( const std::string& name ) 
{
	GL_CHECK( uniforms[ name ] = glGetUniformLocation( program, name.c_str() ) ); 
}

INLINE void Program::AddAttrib( const std::string& name )
{
	GL_CHECK( attribs[ name ] = glGetAttribLocation( program, name.c_str() ) );
}

INLINE void Program::LoadMatrix( const std::string& name, const glm::mat4& t ) const
{
	GL_CHECK( glProgramUniformMatrix4fv( program, uniforms.at( name ), 1, GL_FALSE, glm::value_ptr( t ) ) );
}

INLINE void Program::LoadVec3( const std::string& name, const glm::vec3& v ) const
{
	GL_CHECK( glProgramUniform3fv( program, uniforms.at( name ), 1, glm::value_ptr( v ) ) );
}

INLINE void Program::LoadVec4( const std::string& name, const glm::vec4& v ) const
{
	GL_CHECK( glProgramUniform4fv( program, uniforms.at( name ), 1, glm::value_ptr( v ) ) );
}

INLINE void Program::LoadInt( const std::string& name, int v ) const
{
	GL_CHECK( glProgramUniform1i( program, uniforms.at( name ), v ) );
}

INLINE void Program::Bind( void ) const
{
	GL_CHECK( glUseProgram( program ) );
}

INLINE void Program::Release( void ) const
{
	GL_CHECK( glUseProgram( 0 ) );
}