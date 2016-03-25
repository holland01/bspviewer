#include "effect_shader.h"
#include "q3bsp.h"
#include "io.h"
#include "shader.h"
#include "glutil.h"
#include "renderer/texture.h"
#include "lib/cstring_util.h"
#include <sstream>

//#define G_DUPLICATE_PROGRAMS

static INLINE GLsizei GL_EnumFromStr( const char* str );
static INLINE GLsizei GL_DepthFuncFromStr( const char* str );

namespace {

// struct for debugging shader generation/parsing
struct meta_t
{
	bool logProgramGen = true;
	FILE*	programLog = nullptr;
	uint32_t currLineCount = 0;

	std::string currShaderFile;

	meta_t( void )
	{
		if ( logProgramGen )
			programLog = fopen( "log/shader_gen.txt", "wb" );
	}

	~meta_t( void )
	{
		if ( programLog )
			fclose( programLog );
	}

	void LogShader( const std::string& title,
					const std::string& source,
					uint8_t stage = 0 )
	{
		if ( logProgramGen )
		{
			fprintf( programLog, "[ %i ] [ \n\n %s \n\n%s \n\n ]",
				stage,
				title.c_str(),
				source.c_str() );
		}
	}

	void LogEndShaderGen( void )
	{
		if ( logProgramGen )
		{
			fprintf( programLog,
					 "---------------------------\n\n"
					 "PROGRAM COUNT: %llu"
					 "\n\n-----------------------------",
					 ( unsigned long long ) GNumPrograms() );
		}
	}
};

std::unique_ptr< meta_t > gMeta( new meta_t() );

}

using stageEvalFunc_t = std::function< bool( const char* & buffer, shaderInfo_t* outInfo, shaderStage_t& theStage, char* token ) >;

#define STAGE_READ_FUNC []( const char* & buffer, shaderInfo_t* outInfo, shaderStage_t& theStage, char* token ) -> bool

#define ZEROTOK( t ) ( memset( t, 0, sizeof( char ) * SHADER_MAX_TOKEN_CHAR_LENGTH ) );

// Lookup table we use for each shader/stage command
std::unordered_map< std::string, stageEvalFunc_t > stageReadFuncs =
{
	{
		"surfaceparm",
		STAGE_READ_FUNC
		{
			UNUSED( theStage );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "nodamage" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_NO_DMG;
			}
			else if ( strcmp( token, "nolightmap" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_NO_LIGHTMAP;
			}
			else if ( strcmp( token, "nonsolid" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_NON_SOLID;
			}
			else if ( strcmp( token, "nomarks" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_NO_MARKS;
			}
			else if ( strcmp( token, "trans" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_TRANS;
			}
			else if ( strcmp( token, "nodraw" ) == 0 )
			{
				outInfo->surfaceParms  |= SURFPARM_NO_DRAW;
			}
			else
			{
				return false;
			}

			return true;
		}
	},
	{
		"deformvertexes",
		STAGE_READ_FUNC
		{
			UNUSED( theStage );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "wave" ) == 0 )
			{
				outInfo->deformCmd = VERTEXDEFORM_CMD_WAVE;
			}
			else if ( strcmp( token, "normal" ) == 0 )
			{
				outInfo->deformCmd = VERTEXDEFORM_CMD_NORMAL;
			}
			else if ( strcmp( token, "bulge" ) == 0 )
			{
				outInfo->deformCmd = VERTEXDEFORM_CMD_BULGE;
			}
			else
			{
				return false;
			}

			// Bulge and normal/wave signatures differ significantly, so we separate paths here
			switch ( outInfo->deformCmd )
			{
			case VERTEXDEFORM_CMD_WAVE:
				outInfo->deformParms.data.wave.spread = StrReadFloat( buffer );

				ZEROTOK( token );
				buffer = StrReadToken( token, buffer );

				if ( strcmp( token, "triangle" ) == 0 )
				{
					outInfo->deformFn = VERTEXDEFORM_FUNC_TRIANGLE;
				}
				else if ( strcmp( token, "sin" ) == 0 )
				{
					outInfo->deformFn = VERTEXDEFORM_FUNC_SIN;
				}
				else if ( strcmp( token, "square" ) == 0 )
				{
					outInfo->deformFn = VERTEXDEFORM_FUNC_SQUARE;
				}
				else if ( strcmp( token, "sawtooth" ) == 0 )
				{
					outInfo->deformFn = VERTEXDEFORM_FUNC_SAWTOOTH;
				}
				else if ( strcmp( token, "inversesawtooth" ) == 0 )
				{
					outInfo->deformFn = VERTEXDEFORM_FUNC_INV_SAWTOOTH;
				}

				outInfo->deformParms.data.wave.base = StrReadFloat( buffer );
				outInfo->deformParms.data.wave.amplitude = StrReadFloat( buffer );

				// Normal command has no phase translation
				if ( outInfo->deformCmd == VERTEXDEFORM_CMD_WAVE )
				{
					outInfo->deformParms.data.wave.phase = StrReadFloat( buffer );
				}

				outInfo->deformParms.data.wave.frequency = StrReadFloat( buffer );

				outInfo->deform = true;
				break;

			default:
				MLOG_WARNING_SANS_FUNCNAME( "deformvertexes", "Unsupported vertex deform found!" );
				outInfo->deform = false;
				return false;
				break;
			}

			return true;
		}
	},
	{
		"cull",
		STAGE_READ_FUNC
		{
			UNUSED( theStage );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "back" ) == 0 )
			{
				outInfo->cullFace = GL_BACK;
			}
			else if ( strcmp( token, "none" ) == 0 || strcmp( token, "disable" ) == 0 )
			{
				outInfo->cullFace = GL_NONE;
			}
			else
			{
				// the Q3 Shader Manual states that GL-FRONT is the default if no keyword is specified. The only other keyword
				// that we have available to check after the above conditions is "front" anyway.
				outInfo->cullFace = GL_FRONT;
			}

			return true;
		}
	},
	{
		"nopicmip",
		STAGE_READ_FUNC
		{
			UNUSED( token );
			UNUSED( buffer );
			UNUSED( theStage );

			outInfo->localLoadFlags ^= Q3LOAD_TEXTURE_MIPMAP;
			return true;
		}
	},
	{
		"tesssize",
		STAGE_READ_FUNC
		{
			UNUSED( token );
			UNUSED( theStage );

			outInfo->tessSize = StrReadFloat( buffer );
			return true;
		}
	},
	{
		"q3map_tesssize",
		STAGE_READ_FUNC
		{
			UNUSED( token );
			UNUSED( theStage );

			outInfo->tessSize = StrReadFloat( buffer );
			return true;
		}
	},
	{
		"clampmap",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );
			UNUSED( token );

			buffer = StrReadToken( &theStage.texturePath[ 0 ], buffer );
			theStage.mapCmd = MAP_CMD_CLAMPMAP;
			theStage.mapType = MAP_TYPE_IMAGE;
			return true;
		}
	},
	{
		"map",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );
			UNUSED( token );

			buffer = StrReadToken( &theStage.texturePath[ 0 ], buffer );
			theStage.mapCmd = MAP_CMD_MAP;

			// TODO: add support for this
			if ( strcmp( &theStage.texturePath[ 0 ], "$whiteimage" ) == 0 )
			{
				return true;
			}

			if ( strcmp( &theStage.texturePath[ 0 ], "$lightmap" ) == 0 )
			{
				theStage.mapType = MAP_TYPE_LIGHT_MAP;
			}
			else
			{
				theStage.mapType = MAP_TYPE_IMAGE;
			}

			return true;
		}
	},
	{
		"blendfunc",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );
			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "add" ) == 0 )
			{
				theStage.blendSrc = GL_ONE;
				theStage.blendDest = GL_ONE;
			}
			else if ( strcmp( token, "blend" ) == 0 )
			{
				theStage.blendSrc = GL_SRC_ALPHA;
				theStage.blendDest = GL_ONE_MINUS_SRC_ALPHA;
			}
			else if ( strcmp( token, "filter" ) == 0 )
			{
				theStage.blendSrc = GL_DST_COLOR;
				theStage.blendDest = GL_ZERO;
			}
			else
			{
				GLsizei blendFactor = GL_EnumFromStr( token );
				if ( blendFactor == -1 )
				{
					return false;
				}

				theStage.blendSrc = ( GLenum ) blendFactor;

				ZEROTOK( token );
				buffer = StrReadToken( token, buffer );

				blendFactor = GL_EnumFromStr( token );
				if ( blendFactor == -1 )
				{
					theStage.blendDest = theStage.blendSrc;
					return false;
				}

				theStage.blendDest = ( GLenum ) blendFactor;
			}

			return true;
		}
	},
	{
		"alphafunc",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "ge128" ) == 0 )
			{
				theStage.alphaFunc = ALPHA_FUNC_GEQUAL_128;
			}
			else if ( strcmp( token, "gT0" ) == 0 )
			{
				theStage.alphaFunc = ALPHA_FUNC_GTHAN_0;
			}
			else if ( strcmp( token, "lt128" ) == 0 )
			{
				theStage.alphaFunc = ALPHA_FUNC_LTHAN_128;
			}
			else
			{
				return false;
			}

			return true;
		}
	},
	{
		"rgbgen",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "vertex" ) == 0 )
			{
				theStage.rgbGen = RGBGEN_VERTEX;
			}
			else if ( strcmp( token, "identity" ) == 0 )
			{
				theStage.rgbGen = RGBGEN_IDENTITY;
			}
			else if ( strcmp( token, "identitylighting" ) == 0 )
			{
				theStage.rgbGen = RGBGEN_IDENTITY_LIGHTING;
			}
			else
			{
				return false;
			}

			return true;
		}
	},
	{
		"tcgen",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );
			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "environment" ) == 0 )
			{
				theStage.tcgen = TCGEN_ENVIRONMENT;
			}
			else if ( strcmp( token, "base" ) == 0 )
			{
				theStage.tcgen = TCGEN_BASE;
			}
			else if ( strcmp( token, "lightmap" ) == 0 )
			{
				theStage.tcgen = TCGEN_LIGHTMAP;
			}
			else
			{
				return false;
			}

			return true;
		}
	},
	{
		"tcmod",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			if ( strcmp( token, "scale" ) == 0 )
			{
				effect_t op;

				op.name = "tcModScale";

				float s = StrReadFloat( buffer );
				float t = StrReadFloat( buffer );

				/*
				NOTE: a scale may imply a division by the value, versus a multiplication. I'm not sure...

				if ( s != 0.0f )
					s = 1.0f / s;

				if ( t != 0.0f )
					t = 1.0f / t;
				*/

				op.data.scale2D[ 0 ][ 0 ] = s;
				op.data.scale2D[ 0 ][ 1 ] = 0.0f;

				op.data.scale2D[ 1 ][ 0 ] = 0.0f;
				op.data.scale2D[ 1 ][ 1 ] = t;

				theStage.effects.push_back( op );
			}
			else if ( strcmp( token, "turb" ) == 0 )
			{
				effect_t op;

				op.name = "tcModTurb";

				op.data.wave.base = StrReadFloat( buffer );
				op.data.wave.amplitude = StrReadFloat( buffer );
				op.data.wave.phase = StrReadFloat( buffer );
				op.data.wave.frequency = StrReadFloat( buffer );

				theStage.effects.push_back( op );
			}
			else if ( strcmp( token, "scroll" ) == 0 )
			{
				effect_t op;

				op.name = "tcModScroll";

				op.data.xyzw[ 0 ] = StrReadFloat( buffer );
				op.data.xyzw[ 1 ] = StrReadFloat( buffer );

				theStage.effects.push_back( op );
			}
			else if ( strcmp( token, "rotate" ) == 0 )
			{
				effect_t op;

				op.name = "tcModRotate";

				float angRad = glm::radians( StrReadFloat( buffer ) );

				op.data.rotation2D.transform[ 0 ][ 0 ] =  glm::cos( angRad );
				op.data.rotation2D.transform[ 0 ][ 1 ] = -glm::sin( angRad );

				op.data.rotation2D.transform[ 1 ][ 0 ] =  glm::sin( angRad );
				op.data.rotation2D.transform[ 1 ][ 1 ] =  glm::cos( angRad );

				theStage.effects.push_back( op );
			}
			else
			{
				return false;
			}

			return true;
		}
	},
	{
		"depthfunc",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			GLsizei depthf = GL_DepthFuncFromStr( token );

			if ( depthf == -1 )
			{
				return false;
			}

			theStage.depthFunc = ( GLenum ) depthf;
			return true;
		}
	},
	{
		"depthwrite",
		STAGE_READ_FUNC
		{
			UNUSED( outInfo );
			UNUSED( token );
			UNUSED( buffer );

			theStage.depthPass = true;
			return true;
		}
	}
};

static INLINE GLsizei GL_EnumFromStr( const char* str )
{
	// blending
	if ( strcmp( str, "gl_one_minus_src_alpha" ) == 0 ) return GL_ONE_MINUS_SRC_ALPHA;
	if ( strcmp( str, "gl_one_minus_src_color" ) == 0 ) return GL_ONE_MINUS_SRC_COLOR;
	if ( strcmp( str, "gl_one_minus_dst_alpha" ) == 0 ) return GL_ONE_MINUS_DST_ALPHA;

	if ( strcmp( str, "gl_dst_color" ) == 0 ) return GL_DST_COLOR;
	if ( strcmp( str, "gl_src_color" ) == 0 ) return GL_SRC_COLOR;
	if ( strcmp( str, "gl_src_alpha" ) == 0 ) return GL_SRC_ALPHA;

	if ( strcmp( str, "gl_zero" ) == 0 ) return GL_ZERO;
	if ( strcmp( str, "gl_one" ) == 0 ) return GL_ONE;

	// depth funcs
	if ( strcmp( str, "gl_never" ) == 0 ) return GL_NEVER;
	if ( strcmp( str, "gl_less" ) == 0 ) return GL_LESS;
	if ( strcmp( str, "gl_equal" ) == 0 ) return GL_EQUAL;
	if ( strcmp( str, "gl_lequal" ) == 0 ) return GL_LEQUAL;
	if ( strcmp( str, "gl_greater" ) == 0 ) return GL_GREATER;
	if ( strcmp( str, "gl_notequal" ) == 0 ) return GL_NOTEQUAL;
	if ( strcmp( str, "gl_gequal" ) == 0 ) return GL_GEQUAL;
	if ( strcmp( str, "gl_always" ) == 0 ) return GL_ALWAYS;

	return -1;
}

static INLINE GLsizei GL_DepthFuncFromStr( const char* str )
{
	if ( strcmp( str, "equal" ) == 0 ) return GL_EQUAL;
	if ( strcmp( str, "lequal" ) == 0 ) return GL_LEQUAL;

	// The manual seems to insinuate that gl_ prefixes won't be used for depth functions. However, this is used just in case...
	return GL_EnumFromStr( str );
}



static bool ShaderUsed( const char* header, const Q3BspMap* map )
{
	for ( int i = 0; i < map->data.numShaders; ++i )
	{
		if ( strcmp( map->data.shaders[ i ].name, header ) == 0 )
		{
			return true;
		}
	}

	for ( int i = 0; i < map->data.numFogs; ++i )
	{
		if ( strcmp( map->data.fogs[ i ].name, header ) == 0 )
		{
			return true;
		}
	}

	return false;
}

static const char* SkipLevel( const char* buffer, int8_t targetLevel )
{
	const char *pch = buffer;

	int8_t level = targetLevel;

	while ( *pch )
	{
		switch ( *pch )
		{
		case '{':
			level++;
			break;
		case '}':
			level--;
			if ( level == targetLevel )
			{
				return pch + 1;
			}
			break;
		}

		pch++;
	}

	return pch;
}

// Returns the char count to increment the filebuffer by
static const char* ParseEntry(
	shaderInfo_t* outInfo,
	bool isMapShader,
	bool& used,
	const char* buffer,
	int level,
	const Q3BspMap* map )
{
	char token[ 64 ];

	char first = 0;

	shaderStage_t stage;

	while ( true )
	{
		memset( token, 0, sizeof( token ) );

		if ( !*( buffer = StrReadToken( token, buffer ) ) )
		{
			break;
		}

		first = *buffer;

evaluate_tok:
		// Begin stage?
		if ( *token == '{' )
		{
			level += 1;
			continue;
		}

		// End stage; we done
		if ( *token == '}' )
		{
			// We're back out into the main level, so we're finished with this entry.
			if ( level == 1 )
			{
				break;
			}
			// We're not in the main level, but we're leaving this stage, so decrease our level by 1 and add on to our stageCount
			else
			{
				outInfo->stageBuffer.push_back( stage );
				stage = shaderStage_t();
				outInfo->stageCount += 1;
				level -= 1;
				continue;
			}
		}

		// No invalid tokens ( e.g., spaces, indents, comments, etc. ); so, this must be a header
		if ( level == 0 )
		{
			strcpy( &outInfo->name[ 0 ], token );

			// Ensure we have a valid shader which a) we know is used by the map
			// and b) hasn't already been read
			used = ( ShaderUsed( &outInfo->name[ 0 ], map ) || isMapShader );

			if ( !used )
			{
				return SkipLevel( buffer, level );
			}

			continue;
		}

		const std::string strToken( token );
		if ( stageReadFuncs.find( strToken ) == stageReadFuncs.end() )
		{
			continue;
		}

		if ( !stageReadFuncs.at( strToken )( buffer, outInfo, stage, token ) )
		{
			goto evaluate_tok;
		}
	}

	if ( *buffer == first )
	{
		buffer++;
	}

	return buffer;
}

static INLINE void AddDiscardIf( std::vector< std::string >& fragmentSrc, const std::string& discardPredicate )
{
	fragmentSrc.push_back( "\tif ( " + discardPredicate + " )" );
	fragmentSrc.push_back( "\t{" );
	fragmentSrc.push_back( "\t\tdiscard;" );
	fragmentSrc.push_back( "\t}" );
}

static INLINE std::string SampleTexture2D( const std::string& samplerName, const std::string& coords )
{
#ifdef G_USE_GL_CORE
	std::string fname( "texture" );
#else
	std::string fname( "texture2D" );
#endif
	return fname + "( " + samplerName + ", " + coords + " )";
}

static INLINE std::string SampleFromTexture( const std::string& destCoords,
									 const std::string& destSample,
									 const std::string& srcCoords,
									 const std::string& srcSample,
									 const std::string& scaleParams,
									 const std::string& transform )
{

	std::stringstream ss;

	ss << "\t" << destCoords << " = " << "mod( " << srcCoords << ", vec2( 0.99 ) ) * " << scaleParams
	   << " * " << transform << ".zw + " << transform << ".xy;\n";

	ss << "\t" << destSample << " = " << SampleTexture2D( srcSample, destCoords ) << ";";

	return ss.str();
}


static INLINE std::string WriteFragment( const std::string& value )
{
#ifdef G_USE_GL_CORE
	return "fragment = " + value + ";";
#else
	return "gl_FragColor = " + value + ";";
#endif
}

static INLINE std::string DeclAttributeVar( const std::string& name, const std::string& type, const int32_t location = -1 )
{
#ifdef G_USE_GL_CORE
	std::string decl("in " + type + " " + name + ";");

#	ifdef __linux__
	UNUSED( location );
#else
	if ( location != -1 )
	{
		decl = "layout( location = " + std::to_string( location ) + " ) " + decl;
	}
#	endif
	return decl;
#else
	UNUSED( location );
	return "attribute " + type + " " + name + ";";
#endif
}

static INLINE std::string DeclTransferVar( const std::string& name,
										   const std::string& type,
										   const std::string& transferMode="" )
{
#ifdef G_USE_GL_CORE
	return transferMode + " " + type + " " + name + ";";
#else
	UNUSED( transferMode );
	return "varying " + type + " " + name + ";";
#endif
}

static INLINE std::string DeclCoreTransforms( void )
{
	return "uniform mat4 modelToView;\n"
		   "uniform mat4 viewToClip;";
}

static INLINE std::string DeclGammaConstant( void )
{
	return "const float gamma = 1.0 / 3.3;";
}

static INLINE void DeclCoreTransforms(	std::vector< std::string >& destShaderSrc,
											 std::vector< std::string >& uniforms,
											 size_t offset )
{
	uniforms.push_back( "modelToView" );
	uniforms.push_back( "viewToClip" );

	destShaderSrc.insert( destShaderSrc.begin() + offset,
		{ "uniform mat4 modelToView;", "uniform mat4 viewToClip;" }
	);
}

static INLINE std::string DeclPrecision( void )
{
#ifdef G_USE_GL_CORE
	return "\n";
#else
	return "precision highp float;";
#endif
}

static INLINE std::string GammaCorrect( const std::string& colorVec )
{
	std::stringstream ss;

	ss << "\t" << colorVec << ".r = pow( " << colorVec << ".r, gamma );\n"
	   << "\t" << colorVec << ".g = pow( " << colorVec << ".g, gamma );\n"
	   << "\t" << colorVec << ".b = pow( " << colorVec << ".b, gamma );";

	return ss.str();
}

static INLINE bool UsesColor( const shaderStage_t& stage )
{
	return stage.rgbGen == RGBGEN_VERTEX;
}

static INLINE void WriteTexture(
		std::vector< std::string >& fragmentSrc,
		const shaderStage_t& stage,
		const char* discardPredicate )
{
	std::string sampleTextureExpr;

	if ( stage.mapCmd == MAP_CMD_CLAMPMAP )
		fragmentSrc.push_back( "\tst = clamp( applyTransform( st ), imageTransform.xy, applyTransform( vec2( 0.99 ) ) );" );
	else
		fragmentSrc.push_back( "\tst = applyTransform( mod( st, vec2( 0.99 ) ) );" );

	sampleTextureExpr = SampleTexture2D( "sampler0", "st" );

	std::stringstream colorAssign;

	colorAssign << "\tvec4 color = ";

	// Some shader entries will incorporate specific alpha values
	if ( stage.alphaGen != 0.0f )
	{
		fragmentSrc.push_back( "\tconst float alphaGen = " + std::to_string( stage.alphaGen ) + std::to_string( ';' ) );

		colorAssign << sampleTextureExpr << ".rgb, alphaGen )";

		if ( UsesColor( stage ) )
		{
			 colorAssign << " * vec4( frag_Color.rgb, alphaGen )";
		}
	}
	else
	{
		colorAssign << sampleTextureExpr;

		if ( UsesColor( stage ) )
		{
			colorAssign <<  " * frag_Color";
		}
	}

	colorAssign << ";";

	fragmentSrc.push_back( colorAssign.str() );

	// Is used occasionally, for example in situations like a bad alpha value.
	if ( discardPredicate )
		AddDiscardIf( fragmentSrc, std::string( discardPredicate ) );

	// Gamma correction
	fragmentSrc.insert( fragmentSrc.end(), GammaCorrect( "color" ) );

	fragmentSrc.push_back( "\t" + WriteFragment( "color" ) );
}

 // Quick subroutine enabling the calculation of environment map;
// uses a simple form of displacement mapping to achieve desired results.
static void AddCalcEnvMap( std::vector< std::string >& destGLSL,
							   const std::string& vertexID,
							   const std::string& normalID,
							   const std::string& eyeID )
{
	destGLSL.insert( destGLSL.end(),
	{
		"\tvec3 dirToEye = normalize( " + eyeID + " - " + vertexID + " );",
		"\tvec3 R = 2.0 * " + normalID + " * dot( dirToEye, " + normalID + " ) - dirToEye;",
		"\tvec2 displace = R.yz * 0.5;",
		"\tvec2 st = vec2( 0.5 ) + vec2( displace.x, -displace.y );"
	} );
}

static INLINE std::string JoinLines( std::vector< std::string >& lines )
{
	std::stringstream shaderSrc;

	for ( std::string& line: lines )
		shaderSrc << line << '\n';

	// Append the end bracket for the main function
	shaderSrc << '}';

	return shaderSrc.str();
}

static std::string GenVertexShader( shaderStage_t& stage,
									const std::string& texCoordName,
									std::vector< std::string >& attribs )
{

	size_t vertTransferOffset = 3;
	size_t vertAttrOffset = 2;

	uint32_t attribLocCounter = 0;

	// Vertex shader...
	// if we're not using core the last args for both the attribute and transfer
	// decl funcs won't be written; they'll likely just get optimized away
	std::vector< std::string > vertexSrc =
	{
		DeclAttributeVar( "position", "vec3", attribLocCounter++ ),
		DeclAttributeVar( texCoordName, "vec2", attribLocCounter++ ),
		DeclTransferVar( "frag_Tex", "vec2", "out" ),
		DeclCoreTransforms(),
		"void main(void) {",
	};

	if ( stage.tcgen == TCGEN_ENVIRONMENT )
	{
		vertexSrc.insert( vertexSrc.begin() + vertAttrOffset++, DeclAttributeVar( "normal", "vec3", attribLocCounter++ ) );
		attribs.push_back( "normal" );
	}

	vertexSrc.push_back( "\tgl_Position = viewToClip * modelToView * vec4( position, 1.0 );" );

	if ( stage.tcgen == TCGEN_ENVIRONMENT )
	{
		AddCalcEnvMap( vertexSrc, "position", "normal", "vec3( -modelToView[ 3 ] )" );
		vertexSrc.push_back( "\tfrag_Tex = st;" );
	}
	else
	{
		vertexSrc.push_back( "\tfrag_Tex = " + texCoordName + ";" );
	}

	if ( UsesColor( stage ) )
	{
		vertexSrc.insert( vertexSrc.begin() + vertAttrOffset++, DeclAttributeVar( "color", "vec4", attribLocCounter++ ) );
		vertexSrc.insert( vertexSrc.begin() + vertTransferOffset++, DeclTransferVar( "frag_Color", "vec4", "out" ) );
		vertexSrc.push_back( "\tfrag_Color = color;" );
		attribs.push_back( "color" );
	}

	return JoinLines( vertexSrc );
}

static std::string GenFragmentShader( shaderStage_t& stage,
							   std::vector< std::string >& uniforms )
{
	size_t fragTransferOffset = 2;
	size_t fragUnifOffset = 3;

	// Fragment shader....
	// Unspecified alphaGen implies a default 1.0 alpha channel
	std::vector< std::string > fragmentSrc =
	{
		DeclPrecision(),
		"const float gamma = 1.0 / 2.2;",
		DeclTransferVar( "frag_Tex", "vec2", "in" ),
#ifdef G_USE_GL_CORE
		DeclTransferVar( "fragment", "vec4", "out" ),
#endif
		"void main(void) {"
	};

	// Add our base image data
	std::initializer_list<std::string> data  =
	{
		"uniform sampler2D sampler0;",
		"uniform vec4 imageTransform;",
		"uniform vec2 imageScaleRatio;",
		"vec2 applyTransform(in vec2 coords) {",
		"\treturn coords * imageTransform.zw * imageScaleRatio + imageTransform.xy;",
		"}"
	};

	fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, data );

	fragmentSrc.push_back( "\tvec2 st = frag_Tex;" );

	if ( UsesColor( stage ) )
	{
		fragmentSrc.insert( fragmentSrc.begin() + fragTransferOffset++, DeclTransferVar( "frag_Color", "vec4", "in" ) );
	}

	for ( const effect_t& op: stage.effects )
	{
		// Modify the texture coordinate as necessary before we write to the texture
		if ( op.name == "tcModTurb" )
		{
			fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, "uniform float tcModTurb;" );
			fragmentSrc.push_back( "\tst *= tcModTurb;" );
			uniforms.push_back( "tcModTurb" );
		}
		else if ( op.name == "tcModScroll" )
		{
			fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, "uniform vec4 tcModScroll;" );
			fragmentSrc.push_back( "\tst += tcModScroll.xy * tcModScroll.zw;" );
			uniforms.push_back( "tcModScroll" );
		}
		else if ( op.name == "tcModRotate" )
		{
			fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, "uniform mat2 texRotate;" );
			fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, "uniform vec2 texCenter;" );
			fragmentSrc.push_back( "\tst += texRotate * ( frag_Tex - texCenter );" );

			uniforms.push_back( "texRotate" );
			uniforms.push_back( "texCenter" );
		}
		else if ( op.name == "tcModScale" )
		{
			fragmentSrc.insert( fragmentSrc.begin() + fragUnifOffset, "uniform mat2 tcModScale;" );
			fragmentSrc.push_back( "\tst = tcModScale * st;" );
			uniforms.push_back( "tcModScale" );
		}
	}

	switch ( stage.alphaFunc )
	{
	case ALPHA_FUNC_UNDEFINED:
		WriteTexture( fragmentSrc, stage, nullptr );
		break;
	case ALPHA_FUNC_GEQUAL_128:
		WriteTexture( fragmentSrc, stage, "color.a < 0.5" );
		break;
	case ALPHA_FUNC_GTHAN_0:
		WriteTexture( fragmentSrc, stage, "color.a == 0" );
		break;
	case ALPHA_FUNC_LTHAN_128:
		WriteTexture( fragmentSrc, stage, "color.a >= 0.5" );
		break;
	}

	return JoinLines( fragmentSrc );
}

static void LoadStageTexture( glm::ivec2& maxDims, std::vector< gImageParams_t >& images, shaderInfo_t& info, int i,
							  const gSamplerHandle_t& sampler, const mapData_t* map )
{
	shaderStage_t& stage = info.stageBuffer[ i ];

	if ( stage.mapType == MAP_TYPE_IMAGE )
	{
		gImageParams_t img;
		img.sampler = sampler;

		// If a texture atlas is being used as a substitute for a texture array,
		// this won't matter.
		std::string texFileRoot( map->basePath );
		std::string texRelativePath( &stage.texturePath[ 0 ], strlen( &stage.texturePath[ 0 ] ) );
		texFileRoot.append( texRelativePath );

		// If it's a tga file and we fail, then chances are there is a jpeg duplicate
		// of it that we can fall back on

		if ( !GLoadImageFromFile( texFileRoot.c_str(), img ) )
		{
			std::string ext;
			size_t index;
			if ( File_GetExt( ext, &index, texFileRoot ) && ext == "tga" )
			{
				texFileRoot.replace( index, 4, ".jpg" );
				if ( !GLoadImageFromFile( texFileRoot, img ) )
				{
					// If we fail second try, just leave it: it will be rendered as a dummy
					MLOG_WARNING( "TGA image asset request. Not found; tried jpeg as an alternative - no luck. File \"%s\"", texFileRoot.c_str() );
					return;
				}
			}
		}

		// We need the highest dimensions out of all images for the texture array
		maxDims.x = glm::max( img.width, maxDims.x );
		maxDims.y = glm::max( img.height, maxDims.y );

		// This index will persist in the texture array it's going into
		stage.textureIndex = images.size();

		images.push_back( img );
	}
}

/*!
	We unfortunately can't use lambdas with the File_IterateDirTree API at the moment,
	primarily due to emscripten complications.

	Considering that the result would compile down to something like this, anyway, though
	we're probably not losing much overhead.
*/
namespace {

struct parseArgs_t
{
	static Q3BspMap* map;

	static int ReadShaderFile( const filedata_t data )
	{
		// Something's probably wrong if there's no data,
		// so let's just end it.
		if ( !data )
		{
			return FILE_CONTINUE_TRAVERSAL;
		}

		if ( !map )
		{
			MLOG_ERROR( "No reference to bsp map data given; aborting traversal" );
			return FILE_STOP_TRAVERSAL;
		}

		std::vector< uint8_t > fileBuffer;
		bool isMapShader = false;
		{
			std::string filepath( ( const char* ) data, strlen( ( const char* )data ) );

			std::string ext;
			bool extFound = File_GetExt( ext, nullptr, filepath );

			if ( !extFound )
			{
				MLOG_WARNING( "No extension found for file \'%s\'; moving on", filepath.c_str() );
				return FILE_CONTINUE_TRAVERSAL;
			}

			if ( ext != "shader" )
			{
				MLOG_WARNING( "File \'%s\' does not have shader extension; moving on", filepath.c_str() );
				return FILE_CONTINUE_TRAVERSAL;
			}

			if ( !File_GetBuf( fileBuffer, filepath ) )
			{
				MLOG_WARNING( "Could not open shader file \'%s\'", filepath.c_str() );
				return FILE_CONTINUE_TRAVERSAL;
			}

			gMeta->currShaderFile = filepath;

			isMapShader = map->IsMapOnlyShader( filepath );
		}

		gMeta->currLineCount = 0;

		const char* pChar = ( const char* ) &fileBuffer[ 0 ];
		const char* end = ( const char* ) &fileBuffer[ fileBuffer.size() - 1 ];
		ptrdiff_t range = ( ptrdiff_t )( end - pChar );

		while ( *pChar && range > 0 )
		{
			shaderInfo_t entry;

			bool used = false;
			entry.localLoadFlags = 0;
			pChar = ParseEntry( &entry, isMapShader, used, pChar, 0, map );

			if ( used )
			{
				map->effectShaders.insert( shaderMapEntry_t( std::string( &entry.name[ 0 ], strlen( &entry.name[ 0 ] ) ), entry ) );
			}

			range = ( ptrdiff_t )( end - pChar );
		}

		return FILE_CONTINUE_TRAVERSAL;
	}
};

Q3BspMap* parseArgs_t::map = nullptr;

}

std::string S_GetGLSLHeader( void )
{
#ifdef G_USE_GL_CORE
#	ifdef __linux__
		return "#version 130\n";
#	else
		return "#version 330";
#	endif // __linux__
#else
	return "#version 100";
#endif
}

//! JoinLines function call will append
//! the ending bracket for the main function,
//! so we don't have to worry about it here.
std::string S_MainVertexShader( void )
{
	std::vector< std::string > sourceLines =
	{
		DeclAttributeVar( "position", "vec3", 0 ),
		DeclAttributeVar( "tex0", "vec2", 1 ),
		DeclAttributeVar( "lightmap", "vec2", 2 ),
		DeclAttributeVar( "color", "vec4", 3 ),
		DeclTransferVar( "frag_Color", "vec4", "out" ),
		DeclTransferVar( "frag_Tex", "vec2", "out" ),
		DeclTransferVar( "frag_Lightmap", "vec2", "out" ),
		DeclCoreTransforms(),
		"void main(void) {",
		"\tgl_Position = viewToClip * modelToView * vec4( position, 1.0 );",
		"\tfrag_Color = color;",
		"\tfrag_Lightmap = lightmap;",
		"\tfrag_Tex = tex0;"
	};

	std::string source( JoinLines( sourceLines ) );

	gMeta->LogShader( "Main Vertex", source );

	return source;
}

std::string S_MainFragmentShader( void )
{
	std::vector< std::string > sourceLines =
	{
		DeclPrecision(),
		DeclGammaConstant(),
		DeclTransferVar( "frag_Color", "vec4", "in" ),
		DeclTransferVar( "frag_Tex", "vec2", "in" ),
		DeclTransferVar( "frag_Lightmap", "vec2", "in" ),
		"uniform sampler2D mainImageSampler;",
		"uniform vec2 mainImageImageScaleRatio;",
		"uniform vec4 mainImageImageTransform;",
		"uniform sampler2D lightmapSampler;",
		"uniform vec2 lightmapImageScaleRatio;",
		"uniform vec4 lightmapImageTransform;",
#ifdef G_USE_GL_CORE
		DeclTransferVar( "fragment", "vec4", "out" ),
#endif
		"void main(void) {",
		"\tvec2 texCoords;",
		"\tvec4 image, lightmap, color;",
		SampleFromTexture( "texCoords", "image", "frag_Tex",
			"mainImageSampler", "mainImageImageScaleRatio", "mainImageImageTransform" ),
		SampleFromTexture( "texCoords", "lightmap", "frag_Lightmap",
			"lightmapSampler", "lightmapImageScaleRatio", "lightmapImageTransform" ),
		"\tcolor = frag_Color * image * lightmap;",
		GammaCorrect( "color" ),
		WriteFragment( "color" )
	};

	std::string source( JoinLines( sourceLines ) );

	gMeta->LogShader( "Main Fragment", source );

	return source;
}

/*!
   Main API for the effect shaders. In theory, the user should only have to call this function.
*/
glm::ivec2 S_LoadShaders( Q3BspMap* map, const gSamplerHandle_t& imageSampler, std::vector< gImageParams_t >& textures )
{
	std::string shaderRootDir( map->data.basePath );
	shaderRootDir.append( "scripts/" );

	printf( "Traversing Directory: %s\n", shaderRootDir.c_str() );

	{
		parseArgs_t::map = map;
		File_IterateDirTree( shaderRootDir, parseArgs_t::ReadShaderFile );
		parseArgs_t::map = nullptr;
	}

	for ( auto& entry: map->effectShaders )
	{
		S_GenPrograms( &entry.second );
	}

	if ( gMeta )
	{
		gMeta->LogEndShaderGen();
	}

	glm::ivec2 maxDims( 0 );
	for ( auto& entry: map->effectShaders )
	{
		for ( int i = 0; i < entry.second.stageCount; ++i )
		{
			LoadStageTexture( maxDims,
				textures, entry.second, i, imageSampler, &map->data );
		}
	}

	return maxDims;
}

void S_GenPrograms( shaderInfo_t* shader )
{
	if ( gMeta->logProgramGen )
	{
		fprintf( gMeta->programLog, "------------------------------------------\n%s\n", &shader->name[ 0 ] );
	}

	int j;
	for ( j = 0; j < shader->stageCount; ++j )
	{
		shaderStage_t& stage = shader->stageBuffer[ j ];

		const std::string texCoordName( ( stage.mapType == MAP_TYPE_LIGHT_MAP )? "lightmap": "tex0" );

		std::vector< std::string > attribs = { "position", texCoordName };
		std::vector< std::string > uniforms = {
			"sampler0",
			"imageTransform",
			"imageScaleRatio",
			"modelToView",
			"viewToClip",
		};

		const std::string& vertexString = GenVertexShader( stage, texCoordName, attribs );
		const std::string& fragmentString = GenFragmentShader( stage, uniforms );

		Program* p = new Program( vertexString, fragmentString, uniforms, attribs );

// On the directive: this is good for testing and doing performance comparisons
#ifndef G_DUPLICATE_PROGRAMS
		stage.program = GFindProgramByData( p->attribs, p->uniforms, &stage );
		if ( G_HNULL( stage.program ) )
#endif
		{
			gMeta->LogShader( "Vertex", vertexString, j );
			gMeta->LogShader( "Fragment", fragmentString, j );
			p->stage = &stage;
			stage.program = GStoreProgram( p );
		}
#ifndef G_DUPLICATE_PROGRAMS
		else
		{
			delete p;
		}
#endif
	}
}

bool operator == ( const std::array< char, SHADER_MAX_TOKEN_CHAR_LENGTH >& str1, const char* str2 )
{
	size_t min = glm::min( strlen( str2 ), str1.size() );

	// str1 should have zeros if its char characters are less than SHADER_MAX_TOKEN_CHAR_LENGTH
	if ( min != str1.size() && str1[ min ] != 0 )
		return false;

	for ( uint32_t i = 0; i < min; ++i )
		if ( str2[ i ] != str1[ i ] )
			return false;

	return true;
}
