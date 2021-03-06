#include "effect_shader.h"
#include "q3bsp.h"
#include "io.h"
#include "shader.h"
#include "glutil.h"
#include "renderer/texture.h"
#include "lib/cstring_util.h"
#include "em_api.h"
#include <sstream>

static INLINE GLsizei GL_EnumFromStr( const char* str );
static INLINE GLsizei GL_DepthFuncFromStr( const char* str );

namespace {

using stageEvalFunc_t = std::function< bool( const char* & buffer,
	shaderInfo_t* outInfo, shaderStage_t& theStage, char* token ) >;

// returns true if input is recognized, false if invalid/unrecognized.
// a return false will log whatever is in "token" at the time of return.
#define STAGE_READ_FUNC []( const char* & buffer, shaderInfo_t* outInfo, \
	shaderStage_t& theStage, char* token ) -> bool

#define ZEROTOK( t ) ( memset( t, 0, \
	sizeof( char ) * BSP_MAX_SHADER_TOKEN_LENGTH ) );


const char* ReadStageTexturePath( shaderStage_t& theStage, const char* buffer )
{
	buffer = StrReadToken( &theStage.texturePath[ 0 ], buffer );
	BspData_FixupAssetPath( &theStage.texturePath[ 0 ] );
	return buffer;
}

bool gIsSkyShader = false; // this saves us an O(N) lookup for every shader that's inserted.

// Lookup table we use for each shader/stage command
std::unordered_map< std::string, stageEvalFunc_t > gStageReadFuncs =
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
			else if ( strcmp( token, "sky" ) == 0 )
			{
				outInfo->surfaceParms |= SURFPARM_SKY;

				gIsSkyShader = true;
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

			// Bulge and normal/wave signatures differ significantly,
			// so we separate paths here
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
				else if ( strcmp( token, "inverseSawtooth" ) == 0 )
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
				MLOG_WARNING_SANS_FUNCNAME( "deformvertexes",
					"Unsupported vertex deform found!" );
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
			else if ( strcmp( token, "none" ) == 0
				|| strcmp( token, "disable" ) == 0 )
			{
				outInfo->cullFace = GL_NONE;
			}
			else if ( strcmp( token, "front" ) == 0 )
			{
				// the Q3 Shader Manual states that GL-FRONT is the default
				// if no keyword is specified. The only other keyword
				// that we have available to check after the above conditions
				// is "front" anyway.
				outInfo->cullFace = GL_FRONT;
			}
			else
			{
				return false;
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

			buffer = ReadStageTexturePath( theStage, buffer );

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

			buffer = ReadStageTexturePath( theStage, buffer );

			theStage.mapCmd = MAP_CMD_MAP;

			if ( strcmp( &theStage.texturePath[ 0 ], "$whiteimage" ) == 0 )
			{
				theStage.mapType = MAP_TYPE_WHITE_IMAGE;
			}

			if ( strcmp( &theStage.texturePath[ 0 ], "$lightmap" ) == 0 )
			{
				theStage.mapType = MAP_TYPE_LIGHT_MAP;
			}
			else if ( BspData_GetAssetBaseFromPath( &theStage.texturePath[ 0 ], nullptr ) != BSP_ASSET_BASE_NONE )
			{
				theStage.mapType = MAP_TYPE_IMAGE;
			}
			else
			{
				return false;
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
				GLsizei sourceFactor = GL_EnumFromStr( token );
				if ( sourceFactor == -1 )
				{
					return false;
				}

				ZEROTOK( token );
				buffer = StrReadToken( token, buffer );

				GLsizei destFactor = GL_EnumFromStr( token );
				if ( destFactor == -1 )
				{
					return false;
				}

				theStage.blendSrc = ( GLenum ) sourceFactor;
				theStage.blendDest = ( GLenum ) destFactor;
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

			if ( strcmp( token, "GE128" ) == 0 )
			{
				theStage.alphaFunc = ALPHA_FUNC_GEQUAL_128;
			}
			else if ( strcmp( token, "GT0" ) == 0 )
			{
				theStage.alphaFunc = ALPHA_FUNC_GTHAN_0;
			}
			else if ( strcmp( token, "LT128" ) == 0 )
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

			// rgbgen Vertex has both lowercase and uppercase entries
			StrLower( token );

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
				theStage.rgbGen = RGBGEN_IDENTITY;
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

			// tcmod Scroll or tcmod scroll is possible
			StrLower( token );

			if ( strcmp( token, "scale" ) == 0 )
			{
				effect_t op;

				op.name = "tcModScale";

				float s = StrReadFloat( buffer );
				float t = StrReadFloat( buffer );

			//	if ( s != 0.0f ) s = 1.0f / s;
			//	if ( t != 0.0f ) t = 1.0f / t;

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
	},
	{
		"sort",
		STAGE_READ_FUNC
		{
			UNUSED( theStage );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );

			StrLower( token );

			bool debugPrintInfo = false;
			bool ret = true;

			std::string debugToken;

			if ( strcmp( token, "portal" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_PORTAL;
				debugToken = "Portal";
			}
			else if ( strcmp( token, "sky" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_SKY;
				debugToken = "Sky";
			}
			else if ( strcmp( token, "opaque" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_OPAQUE;
				debugToken = "Opaque";
			}
			else if ( strcmp( token, "banner" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_BANNER;
				debugToken = "Banner";
			}
			else if ( strcmp( token, "underwater" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_UNDERWATER;
				debugToken = "Underwater";
			}
			else if ( strcmp( token, "additive" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_ADDITIVE;
				debugToken = "Additive";
			}
			else if ( strcmp( token, "nearest" ) == 0 )
			{
				outInfo->sort = BSP_SHADER_SORT_NEAREST;
				debugToken = "Nearest";
			}
			else if ( isdigit( *token ) )
			{
				outInfo->sort = ( bspShaderSort_t ) strtol( token, nullptr, 10 );
				debugToken = "Integral";
			}
			else
			{
				if ( debugPrintInfo )
				{
					MLOG_INFO( "Default Fallback. For Shader: %s", &outInfo->name[ 0 ] );
				}

				return false;
			}

			if ( debugPrintInfo ) 
			{
				MLOG_INFO( "%s Sort Found: %lu. For Shader: %s", debugToken.c_str(), 
					outInfo->sort, &outInfo->name[ 0 ] );
			}

			return ret;
		}
	},
	{
		"skyparms",
		STAGE_READ_FUNC
		{
			UNUSED( theStage );

			bool ret = true;

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );
			if ( strcmp( token, "-" ) != 0 )
			{
				MLOG_WARNING_SANS_FUNCNAME( 
					"[%s] skyparms: <farbox> param given, but isn't supported yet",
					&outInfo->name[ 0 ] 
				);
				ret = false;
			}

			outInfo->cloudHeight = StrReadFloat( buffer );
			if ( outInfo->cloudHeight == 0.0f )
			{
				MLOG_WARNING_SANS_FUNCNAME( 
					"[%s] skyparms: <cloudheight> param is either 0 or invalid.",
					&outInfo->name[ 0 ] 
				);
				ret = false;
			}

			MLOG_INFO_ONCE( "%f", outInfo->cloudHeight );

			ZEROTOK( token );
			buffer = StrReadToken( token, buffer );
			if ( strcmp( token, "-" ) != 0 )
			{
				MLOG_WARNING_SANS_FUNCNAME( 
					"[%s] skyparms: <nearbox> param given, but isn't supported yet",
					&outInfo->name[ 0 ] 
				);
				ret = false;
			}

			// Just in case the entry doesn't specify this.
			if ( !( outInfo->surfaceParms & SURFPARM_SKY ) )
			{
				MLOG_WARNING_SANS_FUNCNAME( 
					"[%s] skyparms: surfaceParms check yielded no sky entry...going to fixup in case it won't be found after this",
					&outInfo->name[ 0 ] 
				);
				outInfo->surfaceParms |= SURFPARM_SKY;
			}

			gIsSkyShader = true;

			return ret;
		}
	}
};

} // end namespace

static INLINE GLsizei GL_EnumFromStr( const char* str )
{
	// some gl enum entries in the shader files are lowercase,
	// and some aren't.
	char copytok[ 64 ];
	memset( copytok, 0, sizeof( copytok ) );
	strcpy( copytok, str );
	StrLower( copytok );

	// blending
	if ( strcmp( copytok, "gl_one_minus_src_alpha" ) == 0 )
		return GL_ONE_MINUS_SRC_ALPHA;
	if ( strcmp( copytok, "gl_one_minus_src_color" ) == 0 )
		return GL_ONE_MINUS_SRC_COLOR;
	if ( strcmp( copytok, "gl_one_minus_dst_alpha" ) == 0 )
		return GL_ONE_MINUS_DST_ALPHA;

	if ( strcmp( copytok, "gl_dst_color" ) == 0 ) return GL_DST_COLOR;
	if ( strcmp( copytok, "gl_src_color" ) == 0 ) return GL_SRC_COLOR;
	if ( strcmp( copytok, "gl_src_alpha" ) == 0 ) return GL_SRC_ALPHA;

	if ( strcmp( copytok, "gl_zero" ) == 0 ) return GL_ZERO;
	if ( strcmp( copytok, "gl_one" ) == 0 ) return GL_ONE;

	// depth funcs
	if ( strcmp( copytok, "gl_never" ) == 0 ) return GL_NEVER;
	if ( strcmp( copytok, "gl_less" ) == 0 ) return GL_LESS;
	if ( strcmp( copytok, "gl_equal" ) == 0 ) return GL_EQUAL;
	if ( strcmp( copytok, "gl_lequal" ) == 0 ) return GL_LEQUAL;
	if ( strcmp( copytok, "gl_greater" ) == 0 ) return GL_GREATER;
	if ( strcmp( copytok, "gl_notequal" ) == 0 ) return GL_NOTEQUAL;
	if ( strcmp( copytok, "gl_gequal" ) == 0 ) return GL_GEQUAL;
	if ( strcmp( copytok, "gl_always" ) == 0 ) return GL_ALWAYS;

	return -1;
}

static INLINE GLsizei GL_DepthFuncFromStr( const char* str )
{
	if ( strcmp( str, "equal" ) == 0 ) return GL_EQUAL;
	if ( strcmp( str, "lequal" ) == 0 ) return GL_LEQUAL;

	// The manual seems to insinuate that gl_ prefixes won't be used for depth
	// functions. However, this is used just in case...
	return GL_EnumFromStr( str );
}

static const char* SkipBlockAtLevel( const char* buffer, int8_t targetLevel )
{
	const char* pch = buffer;

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
	const Q3BspMap* map
)
{
	char token[ 64 ];
	shaderStage_t stage;

	while ( true )
	{
		memset( token, 0, sizeof( token ) );
		buffer = StrReadToken( token, buffer );

		// Unlikely (but possible) check for null term
		if ( !( *buffer ) )
		{
			break;
		}

		// Begin stage?
		if ( *token == '{' )
		{
			level += 1;
			continue;
		}

		// End stage; we done
		if ( *token == '}' )
		{
			// We're back out into the main level, so we're finished
			// with this entry.
			if ( level == 1 )
			{
				break;
			}

			// We're not in the main level; we're leaving a shader stage,
			// so decrease our level by 1 and add on to our stageCount
			else
			{
				outInfo->stageBuffer.push_back( stage );

				stage = shaderStage_t();

				outInfo->stageCount += 1;
				level -= 1;

				continue;
			}
		}

		// We've checked for braces already and there's
		// no invalid tokens. So, this must be a header
		if ( level == 0 )
		{
			strcpy( &outInfo->name[ 0 ], token );
			BspData_FixupAssetPath( &outInfo->name[ 0 ] );

			// Ensure we have a valid shader which a)
			// we know is used by the map and b) hasn't
			// already been read
			used = ( map->IsShaderUsed( outInfo ) || isMapShader );

			if ( !used )
			{
				return SkipBlockAtLevel( buffer, level );
			}

			continue;
		}

		StrLower( token );

		const std::string strToken( token );
		if ( gStageReadFuncs.find( strToken ) == gStageReadFuncs.end() )
		{
		//	MLOG_INFO_ONCE( "[ %s ] Did not recognize function \"%s\"", &outInfo->name[ 0 ], token );
			continue;
		}

		if ( !gStageReadFuncs.at( strToken )( buffer, outInfo, stage, token ) )
		{
		//	MLOG_INFO_ONCE( "[ %s ] Did not recognize param \"%s\", for function \"%s\"", &outInfo->name[ 0 ], token, strToken.c_str() );
		}
	}

	return buffer;
}

static void ParseShaderFile( Q3BspMap* map, char* buffer, int size )
{
	bool isMapShader;

	// Get the filepath using our delimiter; use
	// the path to see if this shader is meant to be read
	// only by the current map
	const char* delim = strchr( buffer, '|' );

	if ( !delim )
	{
		MLOG_WARNING( "No delimiter found! aborting" );
		return;
	}

	{
		char tmp[ 1024 ];

		memset( tmp, 0, sizeof( tmp ) );
		memcpy( tmp, buffer, ( ptrdiff_t )( delim - buffer ) );

		std::string path( tmp );

		//MLOG_INFO( "Shader filepath read from buffer: %s", path.c_str() );
		isMapShader = map->IsMapOnlyShader( path );
	}

	// Parse each entry. We use the range/difference method here,
	// since it's possible to skip over the null terminator
	const char* pChar = &delim[ 1 ];
	const char* end = ( const char* ) &buffer[ size - 1 ];
	ptrdiff_t range = ( ptrdiff_t )( end - pChar );

	while ( range > 0 )
	{
		shaderInfo_t entry;

		bool used = false;
		entry.localLoadFlags = 0;
		pChar = ParseEntry( &entry, isMapShader, used, pChar, 0, map );

		if ( used )
		{
			std::array< char, BSP_MAX_SHADER_TOKEN_LENGTH > name;
			memcpy( &name[ 0 ], &entry.name[ 0 ], BSP_MAX_SHADER_TOKEN_LENGTH );

			map->AddEffectShader( entry );

			if ( gIsSkyShader && !gDeformCache.skyShader )
			{
				gDeformCache.skyShader = map->GetShaderInfo( &name[ 0 ] );
				gDeformCache.InitSkyData( gDeformCache.skyShader->cloudHeight );
			}
		}

		gIsSkyShader = false;

		range = ( ptrdiff_t )( end - pChar );
	}
}

static void OnShaderRead( char* buffer, int size, void* param )
{
	Q3BspMap* map = ( Q3BspMap* )param;

	if ( buffer )
	{
		ParseShaderFile( map, buffer, size );
	}
	else
	{
		map->OnShaderReadFinish();
	}
}

/*
 * Main API for the effect shaders. In theory, the user should
 * only have to call this function.
 */

void S_LoadShaders( Q3BspMap* map )
{
#if defined (WEB_WORKER_CLIENT_READSHADERS)
	std::string shaderRootDir( "scripts|" );
	shaderRootDir.append( ASSET_Q3_ROOT );
	shaderRootDir.append( "/scripts" );

	gFileWebWorker.Await( OnShaderRead, "ReadShaders", shaderRootDir, map );
#else
	UNUSED( map );
#endif
}

bool operator == (
	const std::array< char, BSP_MAX_SHADER_TOKEN_LENGTH >& str1,
	const char* str2 )
{
	size_t min = glm::min( strlen( str2 ), str1.size() );

	// str1 should have zeros if its char characters are less than
	// BSP_MAX_SHADER_TOKEN_LENGTH
	if ( min != str1.size() && str1[ min ] != 0 )
	{
		return false;
	}

	for ( uint32_t i = 0; i < min; ++i )
	{
		if ( str2[ i ] != str1[ i ] )
		{
			return false;
		}
	}
	return true;
}
