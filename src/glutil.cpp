#include "glutil.h"
#include "q3bsp.h"
#include "shader.h"
#include "aabb.h"
#include "renderer/texture.h"
#include "effect_shader.h"
#include "renderer/shader_gen.h"
#include <algorithm>

static inline void MapTexCoord( GLint location, intptr_t offset )
{
	GL_CHECK( glEnableVertexAttribArray( location ) );
	GL_CHECK( glVertexAttribPointer( location, 2, GL_FLOAT, GL_FALSE, sizeof( bspVertex_t ), ( void* ) offset ) );
}

static std::map< std::string, std::function< void( const Program& program ) > > attribLoadFunctions =
{
	{
		"position",
		[]( const Program& program ) -> void
		{
			MapVec3( program.attribs.at( "position" ), offsetof( bspVertex_t,
				position ) );
		}
	},
	{
		"normal",
		[]( const Program& program ) -> void
		{
			MapVec3( program.attribs.at( "normal" ), offsetof( bspVertex_t,
				normal ) );
		}
	},
	{
		"color",
		[]( const Program& program ) -> void
		{
			GL_CHECK_WITH_NAME( glEnableVertexAttribArray(
				program.attribs.at( "color" ) ), "attribLoadFunctions" );
			GL_CHECK_WITH_NAME( glVertexAttribPointer(
				program.attribs.at( "color" ),
				4,
				GL_UNSIGNED_BYTE,
				GL_TRUE,
				sizeof( bspVertex_t ),
				( void* ) offsetof( bspVertex_t, color ) ),
				"attribLoadFunctions" );
		}
	},
	{
		"tex0",
		[]( const Program& program ) -> void
		{
			MapTexCoord( program.attribs.at( "tex0" ), sizeof( float ) * 3 );
		}
	},
	{
		"lightmap",
		[]( const Program& program ) -> void
		{
			MapTexCoord( program.attribs.at( "lightmap" ), sizeof( float ) * 5 );
		}
	}
};

/*
static INLINE void SetPixel( byte* dest, const byte* src, int width, int height, int bpp, int srcX, int srcY, int destX, int destY )
{
	int destOfs = ( width * destY + destX ) * bpp;
	int srcOfs = ( width * srcY + srcX ) * bpp;

	for ( int k = 0; k < bpp; ++k )
		dest[ destOfs + k ] = src[ srcOfs + k ];
}

static INLINE void FlipBytes( byte* out, const byte* src, int width, int height, int bpp )
{
	for ( int y = 0; y < height; ++y )
	{
		for ( int x = 0; x < width; ++x )
		{
			SetPixel( out, src, width, height, bpp, x, height - y - 1, x, y );
		}
	}
}
*/

//-------------------------------------------------------------------------------------------------
// Debugging
//-------------------------------------------------------------------------------------------------

#ifdef DEBUG 

static const Program* gDebugBadProgram = nullptr;

void GPrintBadProgram( void )
{
	if ( !gDebugBadProgram )
	{
		return;
	}

	std::stringstream out;

	out << "Vertex: \n" << gDebugBadProgram->vertexSource << "\n"
		<< "Fragment: \n" << gDebugBadProgram->fragmentSource << "\n"
		<< "Attribs:\n";

	for ( auto attrib: gDebugBadProgram->attribs )
	{
		out << "\t[ " << attrib.first << ":" << attrib.second << " ]\n";
	}

	out << "Uniforms:\n";

	for ( auto uniform: gDebugBadProgram->uniforms )
	{
		out << "\t[ " << uniform.first << ":" << uniform.second << " ]\n";
	}

	MLOG_INFO( "%s", out.str().c_str() );

	gDebugBadProgram = nullptr;
}

bool GHasBadProgram( void )
{
	return !!gDebugBadProgram;
}

//-------------------------------------------------------------------------------------------------

#define GLSTATECHECK_UNDEFINED -1

#define GLSTATECHECK_VALUE( name, a ) 																					\
	"[" name "]: " << std::to_string( a )

#define GLSTATECHECK_1I( queryEnum, defaultValue ) 																		\
	{																													\
		GLint ret;																										\
		GL_CHECK( glGetIntegerv( ( queryEnum ), &ret ) );																\
		stateCheckBuffer << "\t"  << GLSTATECHECK_VALUE( #queryEnum, ret ) 												\
						 << ",  " << GLSTATECHECK_VALUE( "DEFAULT = " #defaultValue, defaultValue ) << "\n";			\
	}

#define GLSTATECHECK_NAME_1I( queryEnum, defaultName, defaultValue ) 													\
	{																													\
		GLint ret;																										\
		GL_CHECK( glGetIntegerv( ( queryEnum ), &ret ) );																\
		stateCheckBuffer << "\t"  << GLSTATECHECK_VALUE( #queryEnum, ret ) 												\
						 << ",  " << GLSTATECHECK_VALUE( "DEFAULT = " defaultName, defaultValue ) << "\n";				\
	}

#define GLSTATECHECK_1B( queryEnum, defaultValue ) 																		\
	{																													\
		GLboolean ret;																									\
		GL_CHECK( glGetBooleanv( ( queryEnum ), &ret ) );																\
		stateCheckBuffer 	<< "\t" << GLSTATECHECK_VALUE( #queryEnum, ret ) 											\
							<< ", " << GLSTATECHECK_VALUE( "DEFAULT = " #defaultValue, defaultValue )  					\
						    << "\n";																					\
	}

#define GLSTATECHECK_1F( queryEnum, defaultValue ) 																		\
	{																													\
		GLfloat ret;																									\
		GL_CHECK( glGetFloatv( ( queryEnum ), &ret ) );																	\
		stateCheckBuffer << "\t"  << GLSTATECHECK_VALUE( #queryEnum, ret ) 												\
						 << ",  " << GLSTATECHECK_VALUE( "DEFAULT = " #defaultValue, defaultValue ) << "\n";			\
	}

#define GLSTATECHECK_NAME_1F( queryEnum, defaultName, defaultValue ) 													\
	{																													\
		GLfloat ret;																									\
		GL_CHECK( glGetFloatv( ( queryEnum ), &ret ) );																	\
		stateCheckBuffer << "\t"  << GLSTATECHECK_VALUE( #queryEnum, ret ) 												\
						 << ",  " << GLSTATECHECK_VALUE( "DEFAULT = " #defaultName, defaultValue ) << "\n";				\
	}


#define GLSTATECHECK_VALUE_2( name, a, b ) 		\
 	"[" name "]: ( " << std::to_string( a ) << ", " << std::to_string( b ) << " )"

#define GLSTATECHECK_2F( queryEnum, default0, default1 ) 																\
	{																													\
		GLfloat ret[ 2 ];																								\
		GL_CHECK( glGetFloatv( ( queryEnum ), &ret[ 0 ] ) );															\
		stateCheckBuffer 	<< "\t" << GLSTATECHECK_VALUE_2( #queryEnum, ret[ 0 ], ret[ 1 ] ) 							\
							<< ", " << GLSTATECHECK_VALUE_2( "DEFAULT", default0, default1 ) 							\
							<< "\n";																					\
	}

#define GLSTATECHECK_2I( queryEnum, default0, default1 ) 																\
	{																													\
		GLint ret[ 2 ];																									\
		GL_CHECK( glGetIntegerv( ( queryEnum ), &ret[ 0 ] ) );															\
		stateCheckBuffer 	<< "\t" << GLSTATECHECK_VALUE_2( #queryEnum, ret[ 0 ], ret[ 1 ] ) 							\
							<< ", " << GLSTATECHECK_VALUE_2( "DEFAULT", default0, default1 ) 							\
							<< "\n";																					\
	}

#define GLSTATECHECK_VALUE_4( name, a, b, c, d ) 																		\
 	"[" name "]: ( " << std::to_string( a ) << ", " 																	\
 		 << std::to_string( b ) << ", " 																				\
 		 << std::to_string( c ) << ", "  																				\
 		 << std::to_string( d ) << " )"

#define GLSTATECHECK_4F( queryEnum, default0, default1, default2, default3 ) 											\
	{																													\
		GLfloat ret[ 4 ];																								\
		GL_CHECK( glGetFloatv( ( queryEnum ), &ret[ 0 ] ) );															\
		stateCheckBuffer 	<< "\t" << GLSTATECHECK_VALUE_4( #queryEnum, ret[ 0 ], ret[ 1 ], ret[ 2 ], ret[ 3 ] ) 		\
							<< ",\n\t\t" << GLSTATECHECK_VALUE_4( "DEFAULT", default0, default1, default2, default3 ) 	\
							<< "\n";																					\
	}

#define GLSTATECHECK_4I( queryEnum, default0, default1, default2, default3 ) 											\
	{																													\
		GLint ret[ 4 ];																									\
		GL_CHECK( glGetIntegerv( ( queryEnum ), &ret[ 0 ] ) );															\
		stateCheckBuffer 	<< "\t" << GLSTATECHECK_VALUE_4( #queryEnum, ret[ 0 ], ret[ 1 ], ret[ 2 ], ret[ 3 ] ) 		\
							<< ",\n\t\t" << GLSTATECHECK_VALUE_4( "DEFAULT", default0, default1, default2, default3 ) 	\
							<< "\n";																					\
	}

#define GLSTATECHECK_SECTION( name ) \
	stateCheckBuffer << "\n\n[\t" name "\t]" << "\n\n";

void GStateCheckReport( void )
{
	std::stringstream stateCheckBuffer;

	GLSTATECHECK_SECTION( "A" )

	GLSTATECHECK_1I( GL_ACTIVE_TEXTURE, GL_TEXTURE0 )
	GLSTATECHECK_2F( GL_ALIASED_LINE_WIDTH_RANGE, GLSTATECHECK_UNDEFINED, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_2F( GL_ALIASED_POINT_SIZE_RANGE, GLSTATECHECK_UNDEFINED, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1I( GL_ALPHA_BITS, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1I( GL_ARRAY_BUFFER_BINDING, 0 )
	
	GLSTATECHECK_SECTION( "B" )

	GLSTATECHECK_1B( GL_BLEND, GL_FALSE )
	GLSTATECHECK_4F( GL_BLEND_COLOR, 0, 0, 0, 0 )
	GLSTATECHECK_1I( GL_BLEND_DST_ALPHA, GL_ZERO )
	GLSTATECHECK_1I( GL_BLEND_DST_RGB, GL_ZERO )
	GLSTATECHECK_1I( GL_BLEND_EQUATION_ALPHA, GL_FUNC_ADD )
	GLSTATECHECK_1I( GL_BLEND_EQUATION_RGB, GL_FUNC_ADD )
	GLSTATECHECK_1I( GL_BLEND_SRC_ALPHA, GL_ONE )
	GLSTATECHECK_1I( GL_BLEND_SRC_RGB, GL_ONE )
	GLSTATECHECK_1I( GL_BLUE_BITS, GLSTATECHECK_UNDEFINED )
	
	GLSTATECHECK_SECTION( "C" )

	GLSTATECHECK_4F( GL_COLOR_CLEAR_VALUE, 0, 0, 0, 0 )
	GLSTATECHECK_4F( GL_COLOR_WRITEMASK, 0, 0, 0, 0 )
	// TODO: GL_COMPRESSED_TEXTURE_FORMATS
	GLSTATECHECK_1B( GL_CULL_FACE, GL_FALSE )
	GLSTATECHECK_1I( GL_CULL_FACE_MODE, GL_BACK )
	GLSTATECHECK_1I( GL_CURRENT_PROGRAM, 0 )

	GLSTATECHECK_SECTION( "D" )

	GLSTATECHECK_1I( GL_DEPTH_BITS, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1F( GL_DEPTH_CLEAR_VALUE, 1.0f )
	GLSTATECHECK_1I( GL_DEPTH_FUNC, GL_LESS )
	GLSTATECHECK_2F( GL_DEPTH_RANGE, 0.0f, 1.0f )
	GLSTATECHECK_1B( GL_DEPTH_TEST, GL_FALSE )
	GLSTATECHECK_1B( GL_DEPTH_WRITEMASK, GL_TRUE )
	GLSTATECHECK_1B( GL_DITHER, GL_TRUE )

	GLSTATECHECK_SECTION( "E" )

	GLSTATECHECK_1I( GL_ELEMENT_ARRAY_BUFFER_BINDING, 0 )

	GLSTATECHECK_SECTION( "F" )

	GLSTATECHECK_1I( GL_FRAMEBUFFER_BINDING, 0 )
	GLSTATECHECK_1I( GL_FRONT_FACE, GL_CCW )

	GLSTATECHECK_SECTION( "G" )

	GLSTATECHECK_1I( GL_GENERATE_MIPMAP_HINT, GL_DONT_CARE )
	GLSTATECHECK_1I( GL_GREEN_BITS, GLSTATECHECK_UNDEFINED )

	GLSTATECHECK_SECTION( "I" )

	GLSTATECHECK_1I( GL_IMPLEMENTATION_COLOR_READ_FORMAT, GL_UNSIGNED_BYTE )
	GLSTATECHECK_1I( GL_IMPLEMENTATION_COLOR_READ_TYPE, GL_UNSIGNED_BYTE )

	GLSTATECHECK_SECTION( "L" )

	GLSTATECHECK_1F( GL_LINE_WIDTH, 1.0f )

	GLSTATECHECK_SECTION( "M" )

	GLSTATECHECK_1I( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 8 )
	GLSTATECHECK_1I( GL_MAX_CUBE_MAP_TEXTURE_SIZE, 16 )
	
	GLSTATECHECK_1I( GL_MAX_FRAGMENT_UNIFORM_VECTORS, 16 )
	
	GLSTATECHECK_1I( GL_MAX_RENDERBUFFER_SIZE, 1 )
	
	GLSTATECHECK_1I( GL_MAX_TEXTURE_IMAGE_UNITS, 8 )
	GLSTATECHECK_1I( GL_MAX_TEXTURE_SIZE, 64 )
	
	GLSTATECHECK_1I( GL_MAX_VARYING_VECTORS, 8 )
	
	GLSTATECHECK_1I( GL_MAX_VERTEX_ATTRIBS, 8 )
	GLSTATECHECK_1I( GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 0 )
	GLSTATECHECK_1I( GL_MAX_VERTEX_UNIFORM_VECTORS, 128 )

	GLSTATECHECK_2I( GL_MAX_VIEWPORT_DIMS, GLSTATECHECK_UNDEFINED, GLSTATECHECK_UNDEFINED )

	GLSTATECHECK_SECTION( "N" )

	GLSTATECHECK_1I( GL_NUM_COMPRESSED_TEXTURE_FORMATS, 0 )

	GLSTATECHECK_1I( GL_NUM_SHADER_BINARY_FORMATS, 0 )

	GLSTATECHECK_SECTION( "P" )

	GLSTATECHECK_1I( GL_PACK_ALIGNMENT, 4 )

	GLSTATECHECK_1F( GL_POLYGON_OFFSET_FACTOR, 0.0f )
	GLSTATECHECK_1B( GL_POLYGON_OFFSET_FILL, GL_FALSE )
	GLSTATECHECK_1F( GL_POLYGON_OFFSET_UNITS, 0.0f )

	GLSTATECHECK_SECTION( "R" )

	GLSTATECHECK_1I( GL_RED_BITS, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1I( GL_RENDERBUFFER_BINDING, 0 )

	GLSTATECHECK_SECTION( "S" )

	GLSTATECHECK_1B( GL_SAMPLE_ALPHA_TO_COVERAGE, GL_FALSE )
	GLSTATECHECK_1I( GL_SAMPLE_BUFFERS, 0 )
	GLSTATECHECK_1B( GL_SAMPLE_COVERAGE, GL_FALSE )
	GLSTATECHECK_1B( GL_SAMPLE_COVERAGE_INVERT, GL_FALSE )
	GLSTATECHECK_1F( GL_SAMPLE_COVERAGE_VALUE, 1.0f )
	GLSTATECHECK_1I( GL_SAMPLES, 0 )

	GLSTATECHECK_4I( GL_SCISSOR_BOX, 0, 0, GLSTATECHECK_UNDEFINED, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1B( GL_SCISSOR_TEST, GL_FALSE )

	// TODO: GL_SHADER_BINARY_FORMATS
	GLSTATECHECK_1B( GL_SHADER_COMPILER, GL_FALSE )
	
	GLSTATECHECK_1I( GL_STENCIL_BACK_FAIL, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_BACK_FUNC, GL_ALWAYS )
	GLSTATECHECK_1I( GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_BACK_PASS_DEPTH_PASS, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_BACK_REF, 0 )
	GLSTATECHECK_1I( GL_STENCIL_BACK_VALUE_MASK, 0xFF )
	GLSTATECHECK_1I( GL_STENCIL_BACK_WRITEMASK, 0xFF )
	GLSTATECHECK_1I( GL_STENCIL_BITS, GLSTATECHECK_UNDEFINED )
	GLSTATECHECK_1I( GL_STENCIL_CLEAR_VALUE, 0 )
	GLSTATECHECK_1I( GL_STENCIL_FAIL, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_FUNC, GL_ALWAYS )
	GLSTATECHECK_1I( GL_STENCIL_PASS_DEPTH_FAIL, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_PASS_DEPTH_PASS, GL_KEEP )
	GLSTATECHECK_1I( GL_STENCIL_REF, 0 )
	GLSTATECHECK_1B( GL_STENCIL_TEST, GL_FALSE )
	GLSTATECHECK_1I( GL_STENCIL_VALUE_MASK, 0xFF )
	GLSTATECHECK_1I( GL_STENCIL_WRITEMASK, 0xFF )

	GLSTATECHECK_1I( GL_SUBPIXEL_BITS, 4 )

	GLSTATECHECK_SECTION( "T" )

	GLSTATECHECK_1I( GL_TEXTURE_BINDING_2D, 0 )
	GLSTATECHECK_1I( GL_TEXTURE_BINDING_CUBE_MAP, 0 )

	GLSTATECHECK_SECTION( "U" )

	GLSTATECHECK_1I( GL_UNPACK_ALIGNMENT, 4 )

	GLSTATECHECK_4I( GL_VIEWPORT, 0, 0, GLSTATECHECK_UNDEFINED, GLSTATECHECK_UNDEFINED )

	MLOG_INFO( "%s", stateCheckBuffer.str().c_str() );
}

#else

void GPrintBadProgram( void ) {}
bool GHasBadProgram( void ) { return false; }
void GStateCheckReport( void ) {}

#endif // DEBUG

//-------------------------------------------------------------------------------------------------
// Main API
//-------------------------------------------------------------------------------------------------

static INLINE void DisableAllAttribs( void )
{
	for ( int i = 0; i < 5; ++i )
		GL_CHECK( glDisableVertexAttribArray( i ) );
}

Program::Program( const std::string& vertexShader, const std::string& fragmentShader, const std::vector< std::string >& bindAttribs )
	: program( 0 ),
	  stage( nullptr )
{
	std::string fullVertexShader( GGetGLSLHeader() + "\n" + vertexShader );
	std::string fullFragmentShader( GGetGLSLHeader() + "\n" + fragmentShader );

	GLuint shaders[] =
	{
		CompileShaderSource( fullVertexShader.c_str(), fullVertexShader.size(), GL_VERTEX_SHADER ),
		CompileShaderSource( fullFragmentShader.c_str(), fullFragmentShader.size(), GL_FRAGMENT_SHADER )
	};

	program = LinkProgram( shaders, 2, bindAttribs );
}

Program::Program( const std::string& vertexShader, const std::string& fragmentShader,
	const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs )
	: Program( vertexShader, fragmentShader, attribs )
{
	GenData( uniforms, attribs );
}

Program::Program( const std::vector< char >& vertexShader, const std::vector< char >& fragmentShader,
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs )
		: Program( std::string( &vertexShader[ 0 ], vertexShader.size() ),
				std::string( &fragmentShader[ 0 ], fragmentShader.size() ), attribs )
{
	GenData( uniforms, attribs );
}

Program::Program( const Program& copy )
	: program( copy.program ),
	  uniforms( copy.uniforms ),
	  attribs( copy.attribs )
{
}

Program::~Program( void )
{
	Release();
	GL_CHECK( glDeleteProgram( program ) );
}

void Program::GenData( const std::vector< std::string >& uniforms,
	const std::vector< std::string >& attribs )
{
	uint32_t max = glm::max( attribs.size(), uniforms.size() );
	for ( uint32_t i = 0; i < max; ++i )
	{
		if ( i < attribs.size() )
		{
			AddAttrib( attribs[ i ] );
		}

		if ( i < uniforms.size() )
		{
			AddUnif( uniforms[ i ] );
		}
	}
}

void Program::LoadDefaultAttribProfiles( void ) const
{
	//DisableAllAttribs();

	for ( const auto& attrib: attribs )
	{
		if ( attrib.second != -1 )
		{
			auto it = std::find( disableAttribs.begin(), disableAttribs.end(), attrib.first );

			if ( it != disableAttribs.end() )
			{
				GL_CHECK( glDisableVertexAttribArray( attrib.second ) );
				continue;
			}

			attribLoadFunctions[ attrib.first ]( *this );
		}
#ifdef DEBUG
		else
		{
			gDebugBadProgram = this;
		}
#endif
	}
}

void Program::DisableDefaultAttribProfiles( void ) const
{
	for ( const auto& attrib: attribs )
	{
		if ( attrib.second != -1 )
		{
			GL_CHECK( glDisableVertexAttribArray( attrib.second ) );
		}
	}
}

void Program::LoadAltAttribProfiles( void ) const
{
	DisableAllAttribs();

	for ( const attribProfile_t& profile: altAttribProfiles )
	{
		GL_CHECK( glEnableVertexAttribArray( profile.location ) );
		GL_CHECK( glVertexAttribPointer( profile.location, profile.tupleSize, profile.apiType,
					profile.normalized, profile.stride, ( const GLvoid* )profile.offset ) );

	}
}

void Program::DisableAltAttribProfiles( void ) const
{
	for ( const attribProfile_t& profile: altAttribProfiles )
	{
		GL_CHECK( glDisableVertexAttribArray( profile.location ) );
	}
}

std::vector< std::string > Program::ArrayLocationNames( const std::string& name, int32_t length )
{
	std::vector< std::string > names;
	names.resize( length );

	for ( int32_t i = 0; i < length; ++i )
	{
		names[ i ] = name + "[" + std::to_string( i ) + "]";
	}
	return names;
}

#define __LOAD_VEC( f, name ) for ( const auto& v: ( name ) ) GL_CHECK( ( f )( v.first, 1, glm::value_ptr( v.second ) ) )
#define __LOAD_MAT( f, name ) for ( const auto& m: ( name ) ) GL_CHECK( ( f )( m.first, 1, GL_FALSE, glm::value_ptr( m.second ) ) )
#define __LOAD_VEC_ARRAY( f, name ) for ( const auto& v: ( name ) ) GL_CHECK( ( f )( v.first, v.second.size(), &v.second[ 0 ][ 0 ] ) )
#define __LOAD_SCALAR( f, name ) for ( auto i: ( name ) ) GL_CHECK( ( f )( i.first, i.second ) )

void Program::Bind( void ) const
{
	GL_CHECK( glUseProgram( program ) );

	__LOAD_VEC( glUniform2fv, vec2s );
	__LOAD_VEC( glUniform3fv, vec3s );
	__LOAD_VEC( glUniform4fv, vec4s );

	__LOAD_MAT( glUniformMatrix2fv, mat2s );
	__LOAD_MAT( glUniformMatrix3fv, mat3s );
	__LOAD_MAT( glUniformMatrix4fv, mat4s );

	__LOAD_VEC_ARRAY( glUniform2fv, vec2Array );
	__LOAD_VEC_ARRAY( glUniform3fv, vec3Array );
	__LOAD_VEC_ARRAY( glUniform4fv, vec4Array );

	__LOAD_SCALAR( glUniform1i, ints );
	__LOAD_SCALAR( glUniform1f, floats );
}

#undef __LOAD_VEC
#undef __LOAD_MAT
#undef __LOAD_VEC_ARRAY
#undef __LOAD_SCALAR

void Program::Release( void ) const
{
	GL_CHECK( glUseProgram( 0 ) );

	vec2s.clear();
	vec3s.clear();
	vec4s.clear();

	mat2s.clear();
	mat3s.clear();
	mat4s.clear();

	vec2Array.clear();
	vec3Array.clear();
	vec4Array.clear();

	ints.clear();
	floats.clear();
}

std::string Program::GetInfoString( void ) const
{
	std::stringstream ss;

	ss << "Attributes {" << "\n";
	for ( auto& attrib: attribs )
	{
		ss << "\t" << attrib.first << ": " << attrib.second << ",\n";
	}

	ss << "}, Uniforms {" << "\n";
	for ( auto& unif: uniforms )
	{
		ss << "\t" << unif.first << ": " << unif.second << ",\n";
	}
	ss << "}\n";

	return ss.str();
}

//-------------------------------------------------------------------------------------------------

loadBlend_t::loadBlend_t( GLenum srcFactor, GLenum dstFactor )
{
	GL_CHECK( glGetIntegerv( GL_BLEND_SRC_RGB, ( GLint* ) &prevSrcFactor ) );
	GL_CHECK( glGetIntegerv( GL_BLEND_DST_RGB, ( GLint* ) &prevDstFactor ) );

	GL_CHECK( glBlendFunc( srcFactor, dstFactor ) );
}

loadBlend_t::~loadBlend_t( void )
{
	GL_CHECK( glBlendFunc( prevSrcFactor, prevDstFactor ) );
}

//-------------------------------------------------------------------------------------------------

ImmDebugDraw::ImmDebugDraw( void )
	: 	vbo( MakeGenericBufferObject() ),
		previousSize( 0 ),
		isset( false )
{
	std::unique_ptr< Program > defaultProgram( 
		new Program(
			std::string( GLSL_INLINE(					// vertex
				attribute vec3 position;
				attribute vec4 color;

				uniform mat4 modelToCamera;
				uniform mat4 cameraToClip;

				varying vec4 frag_Color;

				void main()
				{
					gl_Position = cameraToClip * modelToCamera * vec4( position, 1.0 );
					gl_PointSize = 10.0;
					frag_Color = color;
				}
			) ),
			std::string( GLSL_INLINE(					// fragment
				precision mediump float;
				varying vec4 frag_Color;

				void main()
				{
					gl_FragColor = frag_Color;
				}
			) ),
			{
				"modelToCamera",
				"cameraToClip"
			},
			{
				"position",
				"color"
			}
		)
	);

	defaultProgram->AddAltAttribProfile( 
		{ 
			"position", 
			( GLuint ) defaultProgram->attribs[ "position" ], 
			3,
			GL_FLOAT,
			GL_FALSE,
			sizeof( immDebugVertex_t ),
			offsetof( immDebugVertex_t, position )	
		}
	);

	defaultProgram->AddAltAttribProfile(
		{
			"color",
			( GLuint ) defaultProgram->attribs[ "color" ],
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof( immDebugVertex_t ),
			offsetof( immDebugVertex_t, color )	
		}
	);

	shaderPrograms[ "default" ] = std::move( defaultProgram );

	std::unique_ptr< Program > textured( 
		new Program(
			std::string( GLSL_INLINE(					// vertex
				attribute vec3 position;
				attribute vec2 tex0;

				uniform mat4 modelToCamera;
				uniform mat4 cameraToClip;

				varying vec2 frag_Tex;

				void main()
				{
					gl_Position = cameraToClip * modelToCamera * vec4( position, 1.0 );
					gl_PointSize = 10.0;
					frag_Tex = tex0;
				}
			) ),
			std::string( GLSL_INLINE(					// fragment
				precision highp float;
				varying vec2 frag_Tex;

				uniform float gamma;
				uniform sampler2D sampler0;

				// http://www.java-gaming.org/index.php?topic=37583.0
				vec3 srgbEncode( vec3 color, in float gam ) {
				   float r = color.r < 0.0031308 ? 12.92 * color.r : 1.055 * pow( color.r, 1.0 / gam ) - 0.055;
				   float g = color.g < 0.0031308 ? 12.92 * color.g : 1.055 * pow( color.g, 1.0 / gam ) - 0.055;
				   float b = color.b < 0.0031308 ? 12.92 * color.b : 1.055 * pow( color.b, 1.0 / gam ) - 0.055;
				   return vec3( r, g, b );
				}

				vec3 srgbDecode( vec3 color, in float gam ) {
				   float r = color.r < 0.04045 ? ( 1.0 / 12.92 ) * color.r : pow( ( color.r + 0.055 ) * ( 1.0 / 1.055 ), gam );
				   float g = color.g < 0.04045 ? ( 1.0 / 12.92 ) * color.g : pow( ( color.g + 0.055 ) * ( 1.0 / 1.055 ), gam );
				   float b = color.b < 0.04045 ? ( 1.0 / 12.92 ) * color.b : pow( ( color.b + 0.055 ) * ( 1.0 / 1.055 ), gam );
				   return vec3( r, g, b );
				}

				void main()
				{
					vec2 st = frag_Tex;

					float g = clamp( gamma, 1.0, 2.4 );

					gl_FragColor = vec4( srgbEncode( texture2D( sampler0, st ).rgb, g ), 1.0 );
				}
			) ),
			{
				"modelToCamera",
				"cameraToClip",
				"sampler0",
				"gamma"
			},
			{
				"position",
				"tex0"
			}
		)
	);

	shaderPrograms[ "textured" ] = std::move( textured );
}
	
ImmDebugDraw::~ImmDebugDraw( void )
{
	DeleteBufferObject( GL_ARRAY_BUFFER, vbo );
}

void ImmDebugDraw::Begin( void )
{
	previousSize = vertices.size();
	vertices.clear();
}

void ImmDebugDraw::Finalize( bool setIsset )
{
	if ( isset )
	{
		isset = false;
		vertices.push_back( thisVertex );
	}
	else
	{
		isset = setIsset;
	}
}

void ImmDebugDraw::Position( const glm::vec3& position )
{
	thisVertex.position = position;
	Finalize();
}

void ImmDebugDraw::Color( const glm::u8vec4& color )
{
	thisVertex.color = color;
	Finalize();
}

void ImmDebugDraw::End( GLenum mode, const glm::mat4& projection, const glm::mat4& modelView )
{
	Finalize( false );

	GLint lastVbo;
	GL_CHECK( glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &lastVbo ) );

	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, vbo ) );

	if ( !vertices.empty() )
	{
		size_t byteSize = vertices.size() * sizeof( vertices[ 0 ] );

		if ( vertices.size() <= previousSize )
		{
			GL_CHECK( glBufferSubData( GL_ARRAY_BUFFER, 0, byteSize, &vertices[ 0 ] ) );
		}
		else
		{
			GL_CHECK( glBufferData( GL_ARRAY_BUFFER, byteSize, &vertices[ 0 ], GL_DYNAMIC_DRAW ) );
		}
	}

	const Program* defaultProgram = GetProgram();

	defaultProgram->LoadAltAttribProfiles();

	defaultProgram->LoadMat4( "cameraToClip", projection );
	defaultProgram->LoadMat4( "modelToCamera", modelView );

	defaultProgram->Bind();
	GL_CHECK( glDrawArrays( mode, 0, vertices.size() ) );
	defaultProgram->Release();

	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, ( GLuint ) lastVbo ) );

	defaultProgram->DisableAltAttribProfiles();
}
