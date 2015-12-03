#pragma once

#include "common.h"
#include "bsp_data.h"
#include "gldebug.h"
#include "io.h"
#include <array>
#include <tuple>

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

#define GL_CHECK_WITH_NAME( expr, funcname )\
	do\
	{\
		( expr );\
		ExitOnGLError( _LINE_NUM_, #expr, funcname );\
	}\
	while ( 0 )

enum 
{
	GLUTIL_POLYGON_OFFSET_FILL = 1 << 0,
	GLUTIL_POLYGON_OFFSET_LINE = 1 << 1,
	GLUTIL_POLYGON_OFFSET_POINT = 1 << 2,
	GLUTIL_POLYGON_OFFSET_ALL = 0x7,

	GLUTIL_LAYOUT_POSITION = 1 << 0,
	GLUTIL_LAYOUT_COLOR = 1 << 1,
	GLUTIL_LAYOUT_TEX0 = 1 << 2,
	GLUTIL_LAYOUT_LIGHTMAP = 1 << 3,
	GLUTIL_LAYOUT_NORMAL = 1 << 4,
	GLUTIL_LAYOUT_ALL = 0x1F,

	GLUTIL_NUM_ATTRIBS_MAX = 5
};

class GLConfig
{
public:
	// must match the same number used in main.frag
	static const int32_t MAX_MIP_LEVELS = 16; 
};

class Program;
class AABB;

void SetPolygonOffsetState( bool enable, uint32_t polyFlags );

void ImPrep( const glm::mat4& viewTransform, const glm::mat4& clipTransform );
void ImDrawAxes( const float size );
void ImDrawBounds( const AABB& bounds, const glm::vec4& color ); 
void ImDrawPoint( const glm::vec3& point, const glm::vec4& color, float size = 1.0f );

GLuint GenSampler( bool mipmap, GLenum wrap );
void BindTexture( GLenum target, GLuint handle, int32_t offset, 
	int32_t sampler, const std::string& uniform, const Program& program );

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
static INLINE void UpdateBufferObject( GLenum target, GLuint obj, GLuint offset, const std::vector< T >& data, bool bindUnbind ) 
{
	if ( bindUnbind )
	{
		GL_CHECK( glBindBuffer( target, obj ) );
	}

	GL_CHECK( glBufferSubData( target, offset * sizeof( T ), data.size() * sizeof( T ), &data[ 0 ] ) );

	if ( bindUnbind )
	{
		GL_CHECK( glBindBuffer( target, 0 ) );
	}
}

static INLINE void DeleteVertexArray( GLuint vao )
{
    if ( vao )
    {
        GL_CHECK( glDeleteVertexArrays( 1, &vao ) );
    }
}

static INLINE void DeleteBufferObject( GLenum target, GLuint obj )
{
	if ( obj )
	{
		// Unbind to prevent driver from lazy deletion
		GL_CHECK( glBindBuffer( target, 0 ) );
		GL_CHECK( glDeleteBuffers( 1, &obj ) );
	}
}

static INLINE void DrawElementBuffer( GLuint ibo, size_t numIndices )
{
	GL_CHECK( glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo ) );
	GL_CHECK( glDrawElements( GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, nullptr ) );
	GL_CHECK( glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 ) );
}

static INLINE uint32_t Texture_GetMaxMipLevels2D( int32_t baseWidth, int32_t baseHeight )
{
	return glm::min( ( int32_t ) glm::log2( ( float ) baseWidth ), ( int32_t ) glm::log2( ( float ) baseHeight ) );
}

template< typename textureHelper_t >
static INLINE uint32_t Texture_CalcMipLevels2D( const textureHelper_t& tex, int32_t baseWidth, int32_t baseHeight, int32_t maxLevels )
{
	if ( !maxLevels )
	{
		maxLevels = Texture_GetMaxMipLevels2D( baseWidth, baseHeight );
	}

	int32_t w = baseWidth;
	int32_t h = baseHeight;
	int32_t mip;

	for ( mip = 0; h != 1 && w != 1; ++mip )
	{
		tex.CalcMipLevel2D( mip, w, h );

		if ( h > 1 )
		{
			h /= 2;
		}

		if ( w > 1 )
		{
			w /= 2;
		}
	}

	return mip;
}

//---------------------------------------------------------------------
struct texture_t
{
	bool srgb: 1;
	bool mipmap: 1;

	GLuint handle;
	GLuint sampler;
	GLenum wrap;
	GLenum minFilter;
	GLenum magFilter;
	GLenum format;
	GLenum internalFormat;
	GLenum target;
	GLuint maxMip;

	GLsizei width, height, depth, bpp; // bpp is in bytes

	std::vector< byte > pixels;

	texture_t( void );
	~texture_t( void );
	
	void Bind( void ) const;
	
	void Bind( int offset, const std::string& unif, const Program& prog ) const;
	
	void Release( void ) const;
	
	void Release( int offset ) const;
	
	void GenHandle( void );
	
	void LoadCubeMap( void );
	
	void LoadSettings( void );
	
	void Load2D( void );
	
	bool LoadFromFile( const char* texPath, uint32_t loadFlags );
	
	bool SetBufferSize( int width, int height, int bpp, byte fill );

	bool DetermineFormats( void );

	void CalcMipLevel2D( int32_t mip, int32_t width, int32_t height ) const;
};

INLINE void texture_t::CalcMipLevel2D( int32_t mip, int32_t mipwidth, int32_t mipheight ) const
{
	GL_CHECK( glTexImage2D( target, mip, internalFormat, 
				mipwidth, mipheight, 0, format, GL_UNSIGNED_BYTE, &pixels[ 0 ] ) );
}

INLINE void texture_t::GenHandle( void )
{
	if ( !handle )
	{
		GL_CHECK( glGenTextures( 1, &handle ) );
	}
}

INLINE void texture_t::Bind( void ) const
{
	GL_CHECK( glBindTexture( target, handle ) );
}

INLINE void texture_t::Release( void ) const
{
	GL_CHECK( glBindTexture( target, 0 ) );
}

INLINE void texture_t::Release( int offset ) const
{
	GL_CHECK( glActiveTexture( GL_TEXTURE0 + offset ) );
	GL_CHECK( glBindTexture( target, 0 ) );
	GL_CHECK( glBindSampler( offset, 0 ) );
}

//---------------------------------------------------------------------
struct textureArray_t
{
	GLuint handle;

	glm::ivec4 megaDims;

	std::vector< GLuint > samplers;
	std::vector< uint8_t > usedSlices;	// 1 -> true, 0 -> false
	std::vector< glm::vec3 > biases;	// x and y point to sliceWidth / megaWidth and sliceHeight / megaHeight, respectively. z is the slice index

                textureArray_t( GLsizei width, GLsizei height, GLsizei depth, bool genMipLevels );
				
				~textureArray_t( void );

	void			LoadSlice( GLuint sampler, const glm::ivec3& dims, const std::vector< uint8_t >& buffer, bool genMipMaps );
	
	void			Bind( GLuint unit, const std::string& samplerName, const Program& program ) const;
	
	void			Release( GLuint unit ) const;
};

//-------------------------------------------------------------------------------------------------
class Program
{
private:
	GLuint program;

	void GenData( const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs, bool bindTransformsUbo );

public:
	std::map< std::string, GLint > uniforms; 
	std::map< std::string, GLint > attribs;

	std::vector< std::string > disableAttribs; // Cleared on each invocation of LoadAttribLayout

	Program( const std::string& vertexShader, const std::string& fragmentShader );
	
	Program( const std::string& vertexShader, const std::string& fragmentShader, 
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs, bool bindTransformsUbo = true );
	
	Program( const std::vector< char >& vertexShader, const std::vector< char >& fragmentShader, 
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs, bool bindTransformsUbo = true );

	Program( const Program& copy );

	~Program( void );

	void AddUnif( const std::string& name );
	void AddAttrib( const std::string& name );

	void LoadAttribLayout( void ) const;

	void LoadMat4( const std::string& name, const glm::mat4& t ) const;
	
	void LoadMat2( const std::string& name, const glm::mat2& t ) const;
	void LoadMat2( const std::string& name, const float* t ) const;

	void LoadVec2( const std::string& name, const glm::vec2& v ) const;
	void LoadVec2( const std::string& name, const float* v ) const;

	void LoadVec2Array( const std::string& name, const float* v, int32_t num ) const;

	void LoadVec3( const std::string& name, const glm::vec3& v ) const;

	void LoadVec3Array( const std::string& name, const float* v, int32_t num ) const;

	void LoadVec4( const std::string& name, const glm::vec4& v ) const;
	void LoadVec4( const std::string& name, const float* v ) const;

	void LoadVec4Array( const std::string& name, const float* v, int32_t num ) const;

	void LoadInt( const std::string& name, int v ) const;
	void LoadFloat( const std::string& name, float v ) const;

	void Bind( void ) const;
	void Release( void ) const;

	static std::vector< std::string > ArrayLocationNames( const std::string& name, int32_t length );
};

INLINE void Program::AddUnif( const std::string& name ) 
{
	GL_CHECK( uniforms[ name ] = glGetUniformLocation( program, name.c_str() ) ); 
}

INLINE void Program::AddAttrib( const std::string& name )
{
	GL_CHECK( attribs[ name ] = glGetAttribLocation( program, name.c_str() ) );
}

INLINE void Program::LoadMat4( const std::string& name, const glm::mat4& t ) const
{
	GL_CHECK( glProgramUniformMatrix4fv( program, uniforms.at( name ), 1, GL_FALSE, glm::value_ptr( t ) ) );
}

INLINE void Program::LoadMat2( const std::string& name, const glm::mat2& t ) const
{
	GL_CHECK( glProgramUniformMatrix2fv( program, uniforms.at( name ), 1, GL_FALSE, glm::value_ptr( t ) ) );
}

INLINE void Program::LoadMat2( const std::string& name, const float* t ) const
{
	GL_CHECK( glProgramUniformMatrix2fv( program, uniforms.at( name ), 1, GL_FALSE, t ) );
}

INLINE void Program::LoadVec2( const std::string& name, const glm::vec2& v ) const
{
	GL_CHECK( glProgramUniform2fv( program, uniforms.at( name ), 1, glm::value_ptr( v ) ) );
}

INLINE void Program::LoadVec2( const std::string& name, const float* v ) const
{
	GL_CHECK( glProgramUniform2fv( program, uniforms.at( name ), 1, v ) );
}

INLINE void Program::LoadVec2Array( const std::string& name, const float* v, int32_t num ) const
{
	GL_CHECK( glProgramUniform2fv( program, uniforms.at( name ), num, v ) );
}

INLINE void Program::LoadVec3( const std::string& name, const glm::vec3& v ) const
{
	GL_CHECK( glProgramUniform3fv( program, uniforms.at( name ), 1, glm::value_ptr( v ) ) );
}

INLINE void Program::LoadVec3Array( const std::string& name, const float* v, int32_t num ) const
{
	GL_CHECK( glProgramUniform3fv( program, uniforms.at( name ), num, v ) );
}

INLINE void Program::LoadVec4( const std::string& name, const glm::vec4& v ) const
{
	GL_CHECK( glProgramUniform4fv( program, uniforms.at( name ), 1, glm::value_ptr( v ) ) );
}

INLINE void Program::LoadVec4( const std::string& name, const float* v ) const
{
	GL_CHECK( glProgramUniform4fv( program, uniforms.at( name ), 1, v ) );
}

INLINE void Program::LoadVec4Array( const std::string& name, const float* v, int32_t num ) const
{
	GL_CHECK( glProgramUniform4fv( program, uniforms.at( name ), num, v ) );
}


INLINE void Program::LoadInt( const std::string& name, int v ) const
{
	GL_CHECK( glProgramUniform1i( program, uniforms.at( name ), v ) );
}

INLINE void Program::LoadFloat( const std::string& name, float f ) const 
{
	GL_CHECK( glProgramUniform1f( program, uniforms.at( name ), f ) );
}

INLINE void Program::Bind( void ) const
{
	GL_CHECK( glUseProgram( program ) );
}

INLINE void Program::Release( void ) const
{
	GL_CHECK( glUseProgram( 0 ) );
}
//-------------------------------------------------------------------------------------------------
struct loadBlend_t
{
	GLenum prevSrcFactor, prevDstFactor;

	loadBlend_t( GLenum srcFactor, GLenum dstFactor );
   ~loadBlend_t( void );
};
//---------------------------------------------------------------------
struct rtt_t
{
	texture_t	texture;
	GLuint		fbo;
	GLenum		attachment;

	glm::mat4	view;

	rtt_t( GLenum attachment_, const glm::mat4& view_ ) 
		:	fbo( 0 ),
			attachment( attachment_ ),
			view( view_ )
	{
		GL_CHECK( glGenFramebuffers( 1, &fbo ) );
	}

	~rtt_t( void )
	{
		if ( fbo )
		{
			GL_CHECK( glDeleteFramebuffers( 1, &fbo ) );
		}
	}

	void Attach( int32_t width, int32_t height, int32_t bpp )
	{
		texture.mipmap = false;
		texture.wrap = GL_REPEAT;
		texture.SetBufferSize( width, height, bpp, 255 );
		texture.Load2D();
		texture.LoadSettings();

		GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, fbo ) );
		GL_CHECK( glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.handle, 0 ) );

		GLenum fbocheck;
		GL_CHECK( fbocheck = glCheckFramebufferStatus( GL_FRAMEBUFFER ) );
		if ( fbocheck != GL_FRAMEBUFFER_COMPLETE )
		{
			MLOG_ERROR( "FBO check incomplete; value returned is 0x%x", fbocheck );	
		}
		GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
	}

	void Bind( void ) const
	{
		GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, fbo ) );
		GL_CHECK( glDrawBuffer( attachment ) );
	}

	void Release( void ) const
	{
		GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
		GL_CHECK( glDrawBuffer( GL_BACK ) );
	}
};

//---------------------------------------------------------------------
template< typename TRenderer >
struct transformStash_t
{
	const TRenderer& renderer;
	const glm::mat4& view;
	const glm::mat4& proj;
	
	transformStash_t( const TRenderer& renderer_, const glm::mat4& view_, const glm::mat4& proj_ )
		: renderer( renderer_ ), view( view_ ), proj( proj_ )
	{
	}

	~transformStash_t( void )
	{
		renderer.LoadTransforms( view, proj ); 
	}
};

struct viewportStash_t
{
	std::array< GLint, 4 > original; 

	viewportStash_t( GLint originX, GLint originY, GLint width, GLint height )
	{
		GL_CHECK( glGetIntegerv( GL_VIEWPORT, &original[ 0 ] ) );
		GL_CHECK( glViewport( originX, originY, width, height ) );
	}

	~viewportStash_t( void )
	{
		GL_CHECK( glViewport( original[ 0 ], original[ 1 ], original[ 2 ], original[ 3 ] ) );
	}
};
