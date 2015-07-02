#include "renderer.h"
#include "shader.h"
#include "io.h"
#include "math_util.h"
#include "effect_shader.h"
#include "deform.h"
#include <glm/gtx/string_cast.hpp>

struct config_t
{
	bool drawFacesOnly: 1;
	bool drawIrradiance: 1;
	bool drawFacePatches: 1;
};

config_t config = 
{
	false,
	false,
	false
};

static uint64_t frameCount = 0;

static INLINE void AddSurfaceData( drawSurface_t& surf, int faceIndex, const std::vector< mapModel_t >& glFaces )
{
	if ( surf.faceType == BSP_FACE_TYPE_PATCH )
	{
		surf.indexBuffers.insert( surf.indexBuffers.end(), glFaces[ faceIndex ].rowIndices.begin(), glFaces[ faceIndex ].rowIndices.end() );
		surf.indexBufferSizes.insert( surf.indexBufferSizes.end(), glFaces[ faceIndex ].trisPerRow.begin(), glFaces[ faceIndex ].trisPerRow.end() );
	}
	else
	{
		surf.indexBuffers.push_back( &glFaces[ faceIndex ].indices[ 0 ] );
		surf.indexBufferSizes.push_back( glFaces[ faceIndex ].indices.size() );
	}
	if ( surf.shader && surf.shader->deform )
	{
		surf.deformFaces.push_back( &glFaces[ faceIndex ] );
	}
}

//--------------------------------------------------------------
mapModel_t::mapModel_t( void )
	: deform( false ),
	  vboOffset( 0 ),
	  subdivLevel( 0 )
{
}

//--------------------------------------------------------------
drawPass_t::drawPass_t( const Q3BspMap* const& map, const viewParams_t& viewData )
    : isSolid( true ),
	  faceIndex( 0 ), viewLeafIndex( 0 ), 
	  type( PASS_DRAW ), drawType( PASS_DRAW_MAIN ),
	  renderFlags( 0 ),
	  face( nullptr ),
	  leaf( nullptr ),
	  lightvol( nullptr ),
	  shader( nullptr ),
	  view( viewData )
{
    facesVisited.resize( map->data.numFaces, 0 );
}

//--------------------------------------------------------------
lightSampler_t::lightSampler_t( void )
	:	targetPlane( 0.0f, 0.0f, 0.0f, 1.0f ),
		boundsMin( 0.0f ), boundsMax( 0.0f ),
		fbos( { 0, 0 } )
{
	GLint viewport[ 4 ];
	GL_CHECK( glGetIntegerv( GL_VIEWPORT, viewport ) );

	GLint cubeDims = glm::max( viewport[ 2 ], viewport[ 3 ] );

	attachments[ 0 ].mipmap = false;
	attachments[ 0 ].SetBufferSize( viewport[ 2 ], viewport[ 3 ], 4, 0 );
	attachments[ 0 ].Load2D();

	attachments[ 1 ].mipmap = false;
	attachments[ 1 ].SetBufferSize( cubeDims, cubeDims, 4, 255 );
	attachments[ 1 ].LoadCubeMap();

	GL_CHECK( glGenFramebuffers( lightSampler_t::NUM_BUFFERS, &fbos[ 0 ] ) );
	
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, fbos[ 0 ] ) );
	GL_CHECK( glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
		GL_TEXTURE_2D, attachments[ 0 ].handle, 0 ) );
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
}

lightSampler_t::~lightSampler_t( void )
{
	GL_CHECK( glDeleteFramebuffers( lightSampler_t::NUM_BUFFERS, &fbos[ 0 ] ) );
}

void lightSampler_t::Bind( int fbo ) const
{
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, fbos[ fbo ] ) );
	GL_CHECK( glDrawBuffer( GL_COLOR_ATTACHMENT0 ) );
}

void lightSampler_t::Release( void ) const
{
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ) );
	GL_CHECK( glDrawBuffer( GL_BACK ) );
}

// Create a view projection transform which looks up at the sky
// and fills the screen with as much space as possible so the FBO
// can be sampled from
void lightSampler_t::Elevate( const glm::vec3& min, const glm::vec3& max )
{
	glm::vec3 a( glm::vec3( min.x, max.y, max.z ) - max );
	glm::vec3 b( glm::vec3( max.x, max.y, min.z ) - max );
	glm::vec3 n( -glm::cross( a, b ) );

	targetPlane = glm::vec4( n, glm::dot( n, max ) );
	boundsMin = glm::vec2( min.x, min.z );
	boundsMax = glm::vec2( max.x, max.z );

	float w, h;
	float xDist = max.x - min.x;
	float zDist = min.z - max.z;
	float yDist = max.y - min.y;

	glm::vec3 up;

	if ( xDist > zDist )
	{
		w = xDist;
		h = zDist;
		up = glm::vec3( min.x, 0.0f, max.z ) - glm::vec3( min.x, 0.0f, min.z );
	}
	else
	{
		w = zDist;
		h = xDist;
		up = glm::vec3( max.x, 0.0f, max.z ) - glm::vec3( min.x, 0.0f, max.z );
	}
	
	up = glm::normalize( up );

	camera.SetClipTransform( glm::ortho< float >( -w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, 0.0f, 1000000.0f ) );

	glm::vec3 eye( ( min + max ) * 0.5f );
	eye.y += ( max.y - min.y ) * 0.3f;

	glm::vec3 target( eye + glm::vec3( 0.0f, yDist, 0.0f ) );

	camera.SetViewTransform( glm::lookAt( eye, target, up ) );
	camera.SetViewOrigin( eye ); 
}
//--------------------------------------------------------------
BSPRenderer::BSPRenderer( void )
	:	glEffects( {
			{ 
				"tcModTurb", 
				[]( const Program& p, const effect_t& e ) -> void
				{
					float turb = DEFORM_CALC_TABLE( 
					deformCache.sinTable, 
					0,
					e.data.wave.phase,
					glfwGetTime(),
					e.data.wave.frequency,
					e.data.wave.amplitude );
					p.LoadFloat( "tcModTurb", turb );					
				} 
			},
			{
				"tcModScale",
				[]( const Program& p, const effect_t& e ) -> void
				{
					p.LoadMat2( "tcModScale", &e.data.scale2D[ 0 ][ 0 ] );
				}
			},
			{
				"tcModScroll",
				[]( const Program& p, const effect_t& e ) -> void
				{
					p.LoadVec4( "tcModScroll", e.data.xyzw );
				}
			},
			{
				"tcModRotate",
				[]( const Program& p, const effect_t& e ) -> void
				{
					p.LoadMat2( "texRotate", &e.data.rotation2D.transform[ 0 ][ 0 ] );
					p.LoadVec2( "texCenter", e.data.rotation2D.center );
				}
			}
		} ),
		map ( new Q3BspMap() ),
		camera( nullptr ),
		frustum( new Frustum() ),
		transformBlockIndex( 0 ),
		transformBlockObj( 0 ),
		transformBlockSize( sizeof( glm::mat4 ) * 2 ),
		currLeaf( nullptr ),
		vao( 0 ),
		vbo( 0 ),
		deltaTime( 0.0f ),
		frameTime( 0.0f ),
		curView( VIEW_MAIN )
{
	viewParams_t view;
	view.origin = glm::vec3( -131.291901f, -61.794476f, -163.203659f ); /// debug position which doesn't kill framerate

	camera = new InputCamera( view, EuAng() );
	camera->SetPerspective( 45.0f, 16.0f / 9.0f, 1.0f, 5000.0f );
}

BSPRenderer::~BSPRenderer( void )
{
    GL_CHECK( glDeleteVertexArrays( 1, &vao ) );
	DeleteBufferObject( GL_ARRAY_BUFFER, vbo );

    delete map;
    delete frustum;
    delete camera;
}

void BSPRenderer::MakeProg( const std::string& name, const std::string& vertPath, const std::string& fragPath,
		const std::vector< std::string >& uniforms, const std::vector< std::string >& attribs, bool bindTransformsUbo )
{
	std::vector< char > vertex, fragment;
	if ( !File_GetBuf( vertex, vertPath ) )
	{
		MLOG_ERROR( "Could not open vertex shader" );
		return;
	}

	if ( !File_GetBuf( fragment, fragPath ) )
	{
		MLOG_ERROR( "Could not open fragment shader" );
		return;
	}

	glPrograms[ name ] = std::unique_ptr< Program >( new Program( vertex, fragment, uniforms, attribs, bindTransformsUbo ) );
}

void BSPRenderer::Prep( void )
{
	GL_CHECK( glEnable( GL_DEPTH_TEST ) );
	GL_CHECK( glEnable( GL_BLEND ) );
	GL_CHECK( glEnable( GL_FRAMEBUFFER_SRGB ) );

    GL_CHECK( glDepthFunc( GL_LEQUAL ) );
	GL_CHECK( glBlendEquationSeparate( GL_FUNC_ADD, GL_FUNC_ADD ) );
	
	GL_CHECK( glPointSize( 20.0f ) );
	GL_CHECK( glPolygonOffset( 5.0f, 1.0f ) );

	GL_CHECK( glClearColor( 1.0f, 1.0f, 1.0f, 1.0f ) );
	GL_CHECK( glClearDepth( 1.0f ) );

    GL_CHECK( glGenVertexArrays( 1, &vao ) );
    GL_CHECK( glGenBuffers( 1, &vbo ) );
	
	GL_CHECK( glDisable( GL_CULL_FACE ) );

	// Gen transforms UBO
	GL_CHECK( glGenBuffers( 1, &transformBlockObj ) );
	GL_CHECK( glBindBuffer( GL_UNIFORM_BUFFER, transformBlockObj ) );
	GL_CHECK( glBufferData( GL_UNIFORM_BUFFER, transformBlockSize, NULL, GL_STREAM_DRAW ) );
	GL_CHECK( glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( glm::mat4 ), glm::value_ptr( camera->ViewData().clipTransform ) ) );
	GL_CHECK( glBindBufferRange( GL_UNIFORM_BUFFER, UBO_TRANSFORMS_BLOCK_BINDING, transformBlockObj, 0, transformBlockSize ) );
	GL_CHECK( glBindBuffer( GL_UNIFORM_BUFFER, 0 ) );

	// Load main shader glPrograms
	{
		std::vector< std::string > attribs = 
		{
			"position",
			"color",
			"lightmap",
			"tex0",
			"normal"
		};

		std::vector< std::string > uniforms = 
		{
			"fragTexSampler",
			"fragLightmapSampler",
			"fragIrradianceSampler",
			"fragCubeFace"
		};

		MakeProg( "main", "src/main.vert", "src/main.frag", uniforms, attribs, true );

		uniforms.insert( uniforms.end(), {
			"fragAmbient",
			"fragDirectional",
			"fragDirToLight"
		} );

		attribs.push_back( "normal" );

		MakeProg( "model", "src/model.vert", "src/model.frag", uniforms, attribs, true );

		uniforms = 
		{
			"fragRadianceSampler",
			"fragTargetPlane",
			"fragMin",
			"fragMax"
		};

		attribs = 
		{
			"position",
			"normal"
		};

		MakeProg( "irradiate", "src/irradiate.vert", "src/irradiate.frag", uniforms, attribs, true );

		MakeProg( "debug", "src/debug.vert", "src/debug.frag", { "fragColor" }, { "position" }, true );
	}
}

bool BSPRenderer::IsTransFace( int faceIndex, const shaderInfo_t* shader ) const
{
	const bspFace_t* face = &map->data.faces[ faceIndex ];

	if ( face->texture != -1 )
	{
		if ( shader )
		{
			return ( shader->surfaceParms & SURFPARM_TRANS ) != 0;
		}
		else
		{
			return glTextures[ face->texture ].bpp == 4;
		}
	}

	return false;
}

void BSPRenderer::LoadPassParams( drawPass_t& p, int face, passDrawType_t defaultPass ) const
{
	p.face = &map->data.faces[ face ];
	p.faceIndex = face;

	{
		p.shader = map->GetShaderInfo( face );
	}

	if ( p.shader )
	{
		p.drawType = PASS_DRAW_EFFECT;
	}
	else
	{
		p.drawType = defaultPass;
	}
}

void BSPRenderer::Load( const std::string& filepath, uint32_t mapLoadFlags )
{
    map->Read( filepath, 1, mapLoadFlags );
	map->WriteLumpToFile( BSP_LUMP_ENTITIES );

	Shader_LoadAll( &map->data, map->effectShaders, mapLoadFlags );

	//---------------------------------------------------------------------
	// Load Textures:
	// This is just a temporary hack to brute force load assets without taking into account the effect shader files.
	// Now, we find and generate the textures. We first start with the image files.
	//---------------------------------------------------------------------

	GLint oldAlign;
	GL_CHECK( glGetIntegerv( GL_UNPACK_ALIGNMENT, &oldAlign ) );
	GL_CHECK( glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ) );

	glTextures.resize( map->data.numTextures );
	static const char* validImgExt[] = 
	{
		".jpg", ".png", ".tga", ".tiff", ".bmp"
	};

	for ( int t = 0; t < map->data.numTextures; t++ )
	{
		bool success = false;

		std::string fname( map->data.textures[ t ].name );

		const std::string& texPath = map->data.basePath + fname;
		
		// If we don't have a file extension appended in the name,
		// try to find one for it which is valid
		if ( fname.find_last_of( '.' ) == std::string::npos )
		{
			glTextures[ t ].wrap = GL_REPEAT;

			for ( int i = 0; i < SIGNED_LEN( validImgExt ); ++i )
			{
				const std::string& str = texPath + std::string( validImgExt[ i ] );

				if ( glTextures[ t ].LoadFromFile( str.c_str(), mapLoadFlags ) )
				{
					success = true;
					break;
				}
			}
		}
		
		// Stub out the texture for this iteration by continue; warn user
		if ( !success )
		{
			goto FAIL_WARN;
		}

		continue;

FAIL_WARN:
		MLOG_WARNING( "Could not find a file extension for \'%s\'", texPath.c_str() );
	}

	GL_CHECK( glPixelStorei( GL_UNPACK_ALIGNMENT, oldAlign ) );

	// And then generate all of the lightmaps
	glLightmaps.resize( map->data.numLightmaps );

	for ( int l = 0; l < map->data.numLightmaps; ++l )
	{	
		glLightmaps[ l ].SetBufferSize( BSP_LIGHTMAP_WIDTH, BSP_LIGHTMAP_HEIGHT, 3, 0 );
		
		memcpy( &glLightmaps[ l ].pixels[ 0 ], 
			&map->data.lightmaps[ l ].map[ 0 ][ 0 ][ 0 ], sizeof( byte ) * glLightmaps[ l ].pixels.size() );

		glLightmaps[ l ].wrap = GL_REPEAT;
		glLightmaps[ l ].Load2D();
	}

	glDummyTexture.SetBufferSize( 32, 32, 3, 255 );
	glDummyTexture.Load2D();

	//---------------------------------------------------------------------
	// Generate our face/render data
	//---------------------------------------------------------------------

	glFaces.resize( map->data.numFaces );

	std::vector< bspVertex_t > vertexData( &map->data.vertexes[ 0 ], &map->data.vertexes[ map->data.numVertexes ] );
	int indexOffset = ( int ) vertexData.size();

	// cache the data already used for any polygon or mesh faces, so we don't have to iterate through their index/vertex mapping every frame. For faces
	// which aren't of these two categories, we leave them be.
	for ( int i = 0; i < map->data.numFaces; ++i )
	{
		mapModel_t* mod = &glFaces[ i ]; 

		const bspFace_t* face = map->data.faces + i;

		if ( face->type == BSP_FACE_TYPE_MESH || face->type == BSP_FACE_TYPE_POLYGON )
		{
			mod->indices.resize( face->numMeshVertexes, 0 );
			for ( int j = 0; j < face->numMeshVertexes; ++j )
			{
				mod->indices[ j ] = face->vertexOffset + map->data.meshVertexes[ face->meshVertexOffset + j ].offset;
			}
		}
		else if ( face->type == BSP_FACE_TYPE_PATCH )
		{
			mod->vboOffset = ( GLuint ) vertexData.size();
			int width = ( face->size[ 0 ] - 1 ) / 2;
			int height = ( face->size[ 1 ] - 1 ) / 2;
			const shaderInfo_t* shader = map->GetShaderInfo( i );

			GLenum bufferUsage = ( shader && shader->deform )? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

			// ( k, j ) maps to a ( row, col ) index scheme referring to the beginning of a patch 
			int n, m;
			mod->controlPoints.resize( width * height * 9 );

			for ( n = 0; n < width; ++n )
			{
				for ( m = 0; m < height; ++m )
				{
					int baseSource = face->vertexOffset + 2 * m * width + 2 * n;
					int baseDest = ( m * width + n ) * 9;

					for ( int c = 0; c < 3; ++c )
					{
						mod->controlPoints[ baseDest + c * 3 + 0 ] = &map->data.vertexes[ baseSource + c * face->size[ 0 ] + 0 ];
						mod->controlPoints[ baseDest + c * 3 + 1 ] = &map->data.vertexes[ baseSource + c * face->size[ 0 ] + 1 ];
						mod->controlPoints[ baseDest + c * 3 + 2 ] = &map->data.vertexes[ baseSource + c * face->size[ 0 ] + 2 ];
					}
					
					GenPatch( mod, shader, baseDest, ( int ) vertexData.size() );
				}
			}

			const uint32_t L1 = mod->subdivLevel + 1;
			mod->rowIndices.resize( width * height * mod->subdivLevel, 0 );
			mod->trisPerRow.resize( width * height * mod->subdivLevel, 0 );

			for ( size_t row = 0; row < glFaces[ i ].rowIndices.size(); ++row )
			{
				mod->trisPerRow[ row ] = 2 * L1;
				mod->rowIndices[ row ] = &mod->indices[ row * 2 * L1 ];  
			}

			vertexData.insert( vertexData.end(), mod->vertices.begin(), mod->vertices.end() );
		}
	}

	//---------------------------------------------------------------------
	// Generate the index and draw indirect buffers
	//---------------------------------------------------------------------

	

	// Allocate vertex data from map and store it all in a single vbo
	GL_CHECK( glBindVertexArray( vao ) );
	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, vbo ) );
    GL_CHECK( glBufferData( GL_ARRAY_BUFFER, sizeof( bspVertex_t ) * vertexData.size(), &vertexData[ 0 ], GL_DYNAMIC_DRAW ) );

	// NOTE: this vertex layout may not persist when the model program is used; so be wary of that. "main"
	// and "model" should both have the same attribute location values though

	glPrograms[ "main" ]->LoadAttribLayout();

	const bspNode_t* root = &map->data.nodes[ 0 ];

	glm::vec3 min( map->data.nodes[ 0 ].boxMin );
	glm::vec3 max( map->data.nodes[ 0 ].boxMax );

	lightSampler.Elevate( min, max );
}

void BSPRenderer::Sample( uint32_t renderFlags )
{
	// Clear the color buffer to black so that any sample hits outside of the sky 
	// won't contribute anything in the irradiance generation shader. Depth range
	// is changed so that only the sky is seen

	curView = VIEW_LIGHT_SAMPLE;
	lightSampler.Bind( 0 );

	Render( renderFlags );
	lightSampler.Release();

	curView = VIEW_MAIN;
}

void BSPRenderer::Render( uint32_t renderFlags )
{ 
	static auto LDrawList = [ this ]( drawPass_t& pass, drawSurfaceList_t& list ) -> void
	{
		DrawSurfaceList( pass, list.surfaces );
		DrawSurfaceList( pass, list.effectSurfaces );

		list.surfaces.clear();
		list.effectSurfaces.clear();
	};

	static auto LDrawClear = [ this ]( drawPass_t& pass ) -> void
	{
		LDrawList( pass, pass.polymeshes );
		LDrawList( pass, pass.patches );
		pass.shader = nullptr;
		pass.face = nullptr;
	};

	static auto LTraverseDraw = [ this ]( drawPass_t& pass, bool solid ) -> void
	{
		pass.isSolid = solid;
		DrawNode( pass, 0 );
		LDrawClear( pass );
	};

	double startTime = glfwGetTime();

	viewParams_t& viewRef = CameraFromView()->ViewDataMut();
	
	drawPass_t pass( map, viewRef );
	pass.leaf = map->FindClosestLeaf( pass.view.origin );
	pass.renderFlags = renderFlags;
	//pass.type = PASS_BRUSH;

	//DrawNode( pass, 0 );

	// Draw Models and Leaf Faces
	LoadTransforms( pass.view.transform, pass.view.clipTransform );
	{
		frustum->Update( viewRef, true );

		AABB bounds;
		for ( int i = 1; i < map->data.numModels; ++i )
		{
			bspModel_t* model = &map->data.models[ i ];

			bounds.maxPoint = model->boxMax;
			bounds.minPoint = model->boxMin;

			if ( !frustum->IntersectsBox( bounds ) )
				continue;

			for ( int j = 0; j < model->numFaces; ++j )
			{
				if ( pass.facesVisited[ model->faceOffset + j ] )
					continue;
				
				LoadPassParams( pass, model->faceOffset + j, PASS_DRAW_MAIN );
		
				if ( pass.face->type == BSP_FACE_TYPE_PATCH || config.drawFacesOnly )
				{
					DrawFace( pass );
				}
				else 
				{
					drawSurfaceList_t& list = ( pass.face->type == BSP_FACE_TYPE_PATCH )? pass.patches: pass.polymeshes;

					AddSurface( pass.shader, pass.faceIndex, pass.shader? list.effectSurfaces: list.surfaces );
					pass.facesVisited[ pass.faceIndex ] = true;
				}
			}
		}

		LDrawClear( pass );
	}
	
	pass.type = PASS_DRAW;

	LTraverseDraw( pass, true );
	LTraverseDraw( pass, false );
	
	frameTime = glfwGetTime() - startTime;

	frameCount++;
}

void BSPRenderer::Update( float dt )
{
	camera->Update();

	viewParams_t& view = CameraFromView()->ViewDataMut();
	SetNearFar( view.clipTransform, 1.0f, 100000.0f );

    frustum->Update( view, false );

    deltaTime = dt;
}

void BSPRenderer::DrawNode( drawPass_t& pass, int nodeIndex )
{
    if ( nodeIndex < 0 )
    {
		pass.viewLeafIndex = -( nodeIndex + 1 );
        const bspLeaf_t* viewLeaf = &map->data.leaves[ pass.viewLeafIndex ];

        if ( !map->IsClusterVisible( pass.leaf->clusterIndex, viewLeaf->clusterIndex ) )
		{
            return;
		}

		AABB leafBounds;
		leafBounds.maxPoint = glm::vec3( viewLeaf->boxMax.x, viewLeaf->boxMax.y, viewLeaf->boxMax.z );
		leafBounds.minPoint = glm::vec3( viewLeaf->boxMin.x, viewLeaf->boxMin.y, viewLeaf->boxMin.z );

        if ( !frustum->IntersectsBox( leafBounds ) )
		{
			return;
		}

        for ( int i = 0; i < viewLeaf->numLeafFaces; ++i )
        {
            int faceIndex = map->data.leafFaces[ viewLeaf->leafFaceOffset + i ].index;
			
			// if pass.facesVisited[ faceIndex ] is still false after this criteria's
			// evaluations, we'll pick it up on the next pass as it will meet
			// the necessary criteria then.
            if ( pass.facesVisited[ faceIndex ] )
                continue;
			
			LoadPassParams( pass, faceIndex, PASS_DRAW_MAIN );
			
			bool add = ( !pass.isSolid && IsTransFace( pass.faceIndex, pass.shader ) ) 
				    || ( pass.isSolid && !IsTransFace( pass.faceIndex, pass.shader ) );

			if ( add )
			{
				// Only draw individual faces if they're patches, since meshes and polygons
				// can be easily grouped together from the original vbo
				if ( ( pass.face->type == BSP_FACE_TYPE_PATCH && config.drawFacePatches ) || config.drawFacesOnly )
				{
					DrawFace( pass );
				}
				else
				{
					drawSurfaceList_t& list = ( pass.face->type == BSP_FACE_TYPE_PATCH )? pass.patches: pass.polymeshes;
					AddSurface( pass.shader, pass.faceIndex, pass.shader? list.effectSurfaces: list.surfaces );
				}
				pass.facesVisited[ pass.faceIndex ] = true;
			}
			else
			{
				pass.shader = nullptr;
				pass.face = nullptr;
				pass.faceIndex = 0;
			}
		}
    }
    else
    {
        const bspNode_t* const node = &map->data.nodes[ nodeIndex ];
        const bspPlane_t* const plane = &map->data.planes[ node->plane ];

        float d = glm::dot( pass.view.origin, glm::vec3( plane->normal.x, plane->normal.y, plane->normal.z ) );

		// We're in front of the plane if d > plane->distance.
		// If both of these are true, it makes sense to draw what is in front of us, as any 
		// non-solid object can be handled properly by depth if it's infront of the partition plane
		// and we're behind it
        if ( pass.isSolid == ( d > plane->distance ) )
        {
            DrawNode( pass, node->children[ 0 ] );
			DrawNode( pass, node->children[ 1 ] );
		}
        else
        {
            DrawNode( pass, node->children[ 1 ] );
            DrawNode( pass, node->children[ 0 ] );
        }
    }
}

static std::array< glm::vec3, 6 > faceNormals = 
{
	glm::vec3( 1.0f, 0.0f, 0.0f ),
	glm::vec3( -1.0f, 0.0f, 0.0f ),
	glm::vec3( 0.0f, 1.0f, 0.0f ),
	glm::vec3( 0.0f, -1.0f, 0.0f ),
	glm::vec3( 0.0f, 0.0f, 1.0f ),
	glm::vec3( 0.0f, 0.0f, -1.0f ),
};

void BSPRenderer::DrawMapPass( drawPass_t& pass )
{
	int best = 0;
	if ( config.drawIrradiance )
	{
		float cosAng = 0.0f;
		const glm::vec3 drawFaceNormal( pass.face->normal );
		for ( int i = 0; i < 6; ++i )
		{
			float c = glm::clamp( glm::dot( faceNormals[ i ], drawFaceNormal ), 0.0f, 1.0f );
			if ( c > cosAng )
			{
				best = i;
				cosAng = c;
			}
		}

		glPrograms[ "irradiate" ]->LoadAttribLayout();

		lightSampler.Bind( 1 );

		GL_CHECK( glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + best, lightSampler.attachments[ 1 ].handle, 0 ) );

		GL_CHECK( glActiveTexture( GL_TEXTURE0 ) );
		lightSampler.attachments[ 0 ].Bind();
		GL_CHECK( glBindSampler( 0, lightSampler.attachments[ 0 ].sampler ) );

		const Program& irradiate = *( glPrograms[ "irradiate" ].get() );

		irradiate.LoadInt( "fragRadianceSampler", 0 );
		irradiate.LoadVec2( "fragMin", lightSampler.boundsMin );
		irradiate.LoadVec2( "fragMax", lightSampler.boundsMax );
		irradiate.LoadVec4( "fragTargetPlane", lightSampler.targetPlane );
		irradiate.Bind();

		DrawFaceVerts( pass, irradiate );

		irradiate.Release();

		lightSampler.Release();

		lightSampler.attachments[ 0 ].Release();
		GL_CHECK( glBindSampler( 0, 0 ) );
	}
	
	const texture_t* tex0 = nullptr;
	const texture_t* tex1 = nullptr;

	BeginMapPass( pass, &tex0, &tex1 );
	DrawFaceVerts( pass, *( glPrograms[ "main" ].get() ) );
	EndMapPass( pass, tex0, tex1 );
}

void BSPRenderer::BeginMapPass( drawPass_t& pass, const texture_t** tex0, const texture_t** tex1 )
{
	if ( glTextures[ pass.face->texture ].handle )
	{
	 	*tex0 = &glTextures[ pass.face->texture ]; 
	}
	else
	{
		*tex0 = &glDummyTexture;
	}

	if ( pass.face->lightmapIndex >= 0 )
	{
		*tex1 = &glLightmaps[ pass.face->lightmapIndex ];
	}
	else
	{
		*tex1 = &glDummyTexture;
	}

	const Program& main = *( glPrograms[ "main" ].get() );

	( *tex0 )->Bind( 0, "fragTexSampler", main );
	( *tex1 )->Bind( 1, "fragLightmapSampler", main ); 

	main.LoadAttribLayout();
	main.Bind();

	//lightSampler.attachments[ 1 ].Bind( 1, "fragIrradianceSampler", main );
}

void BSPRenderer::EndMapPass( drawPass_t& pass, const texture_t* tex0, const texture_t* tex1 )
{
	glPrograms[ "main" ]->Release();
	
	tex0->Release( 0 );
	tex1->Release( 1 );
	
	//lightSampler.attachments[ 1 ].Release( 1 );
}

void BSPRenderer::AddSurface( const shaderInfo_t* shader, int faceIndex, std::vector< drawSurface_t >& surfList )
{	
	const bspFace_t* face = &map->data.faces[ faceIndex ]; 

	bool add = true;

	for ( drawSurface_t& surf: surfList )
	{
		if ( shader == surf.shader && face->texture == surf.textureIndex 
			&& face->lightmapIndex == surf.lightmapIndex && face->type == surf.faceType )
		{
			AddSurfaceData( surf, faceIndex, glFaces );
			add = false;
			break;
		}
	}

	if ( add )
	{
		drawSurface_t surf;

		surf.shader = shader;
		surf.lightmapIndex = face->lightmapIndex;
		surf.textureIndex = face->texture;
		surf.faceType = face->type;

		AddSurfaceData( surf, faceIndex, glFaces );
		surfList.push_back( std::move( surf ) ); 
	}
}

void BSPRenderer::DrawEffectPass( const drawPass_t& pass, const drawTuple_t& data ) const
{
	const shaderInfo_t* shader = std::get< 2 >( data );
	int lightmapIndex = std::get< 4 >( data );

	// Each effect pass is allowed only one texture, so we don't need a second texcoord
	GL_CHECK( glDisableVertexAttribArray( 3 ) );

	for ( int i = 0; i < shader->stageCount; ++i )
	{			
		const shaderStage_t& stage = shader->stageBuffer[ i ];

		GL_CHECK( glBlendFunc( stage.rgbSrc, stage.rgbDest ) );
		GL_CHECK( glDepthFunc( stage.depthFunc ) );	
		
		const texture_t* tex = nullptr;

		if ( stage.mapType == MAP_TYPE_LIGHT_MAP )
		{
			tex = &glLightmaps[ lightmapIndex ];
		}
		else
		{
			tex = ( stage.texture.handle != 0 )? &stage.texture: &glDummyTexture; 
		}

		if ( stage.tcgen == TCGEN_ENVIRONMENT )
		{
			__nop();
		}

		ImPrep( pass.view.transform, pass.view.clipTransform );

		for ( effect_t e: stage.effects )
		{
			Shader_SetEffectTextureData( e, *tex );
			glEffects.at( e.name )( *( stage.program.get() ), e ); 
		}

		const Program& stageProg = *( stage.program.get() );

		tex->Bind( 0, "sampler0", stageProg );
		stageProg.LoadInt( "sampler0", 0 );
		stageProg.Bind();

		DrawFromTuple( data, pass, stageProg );

		stageProg.Release();
		tex->Release( 0 );
	}

	GL_CHECK( glEnableVertexAttribArray( 3 ) );
	GL_CHECK( glBindTexture( GL_TEXTURE_2D, 0 ) );
	GL_CHECK( glBindSampler( 0, 0 ) );
}

void BSPRenderer::DrawFace( drawPass_t& pass )
{
	GL_CHECK( glBlendFunc( GL_ONE, GL_ZERO ) );
	GL_CHECK( glDepthFunc( GL_LEQUAL ) );

	drawTuple_t data = std::make_tuple( OBJECT_FACE, 
		( const void* ) pass.face, pass.shader, pass.face->texture, pass.face->lightmapIndex );

	switch ( pass.drawType )
	{
		case PASS_DRAW_EFFECT:
			DrawEffectPass( pass, data );
			break;
	
		case PASS_DRAW_MAIN:
			DrawMapPass( pass );
			break;
	}

    pass.facesVisited[ pass.faceIndex ] = 1;
}

void BSPRenderer::DrawSurfaceList( const drawPass_t& pass, const std::vector< drawSurface_t >& list ) const
{	
	const Program& main = *( glPrograms.at( "main" ).get() );

	for ( const drawSurface_t& surf: list )
	{
		if ( surf.shader )
		{
			drawTuple_t tuple = std::make_tuple( OBJECT_SURFACE, 
				( const void* )&surf, surf.shader, surf.textureIndex, surf.lightmapIndex );
			DrawEffectPass( pass, tuple );
		}
		else
		{
			GL_CHECK( glBlendFunc( GL_ONE, GL_ZERO ) );
			GL_CHECK( glDepthFunc( GL_LEQUAL ) );

			const texture_t& tex0 = GetTextureOrDummy( surf.textureIndex, 
				surf.textureIndex >= 0 && glTextures[ surf.textureIndex ].handle != 0, glTextures );
		
			const texture_t& lightmap = GetTextureOrDummy( surf.lightmapIndex,
				surf.lightmapIndex >= 0, glLightmaps );

			tex0.Bind( 0, "fragTexSampler", main );
			lightmap.Bind( 1, "fragLightmapSampler", main );

			main.Bind();
			DrawSurface( surf, main );
			main.Release();

			tex0.Release( 0 );
			lightmap.Release( 1 );
		}
	}
}

void BSPRenderer::DrawFaceVerts( const drawPass_t& pass, const Program& program ) const
{
	const mapModel_t* m = &glFaces[ pass.faceIndex ];

	if ( pass.face->type == BSP_FACE_TYPE_POLYGON || pass.face->type == BSP_FACE_TYPE_MESH )
	{
		GL_CHECK( glDrawElements( GL_TRIANGLES, m->indices.size(), GL_UNSIGNED_INT, &m->indices[ 0 ] ) );
	}
	else if ( pass.face->type == BSP_FACE_TYPE_PATCH )
	{
		if ( pass.shader && pass.shader->deform )
		{
			DeformVertexes( m, pass.shader );
		}
		
		program.LoadAttribLayout();

		GL_CHECK( glMultiDrawElements( GL_TRIANGLE_STRIP, 
			&m->trisPerRow[ 0 ], GL_UNSIGNED_INT, ( const GLvoid** ) &m->rowIndices[ 0 ], m->trisPerRow.size() ) );
	}
}

void BSPRenderer::DeformVertexes( const mapModel_t* m, const shaderInfo_t* shader ) const
{
	std::vector< bspVertex_t > verts = m->vertices;
	
	int32_t stride = m->subdivLevel + 1;
	int32_t numPatchVerts = stride * stride;
	int32_t numPatches = verts.size() / numPatchVerts;

	for ( uint32_t i = 0; i < verts.size(); ++i )
	{
		glm::vec3 n( verts[ i ].normal * GenDeformScale( verts[ i ].position, shader ) );
		verts[ i ].position += n;
	}

	UpdateBufferObject< bspVertex_t >( GL_ARRAY_BUFFER, vbo, m->vboOffset, verts, false );
}

void BSPRenderer::LoadLightVol( const drawPass_t& pass, const Program& prog ) const
{
	if ( pass.lightvol )
	{
		float phi = glm::radians( ( float )pass.lightvol->direction.x * 4.0f ); 
		float theta = glm::radians( ( float )pass.lightvol->direction.y * 4.0f );
		
		glm::vec3 dirToLight( glm::cos( theta ) * glm::cos( phi ), glm::sin( phi ), glm::cos( phi ) * glm::sin( theta ) );
		
		glm::vec3 ambient( pass.lightvol->ambient );
		ambient *= Inv255< float >();

		glm::vec3 directional( pass.lightvol->directional );
		directional *= Inv255< float >();

		prog.LoadVec3( "fragDirToLight", dirToLight );
		prog.LoadVec3( "fragAmbient", ambient );
		prog.LoadVec3( "fragDirectional", directional );
	}
}

/*
int BSPRenderer::CalcLightvolIndex( const drawPass_t& pass ) const
{
	const glm::vec3& max = map->data.models[ 0 ].boxMax;
	const glm::vec3& min = map->data.models[ 0 ].boxMin;
		 
	glm::vec3 input;
	input.x = glm::abs( glm::floor( pass.view.origin.x * Inv64< float >() ) - glm::ceil( min.x * Inv64< float >() ) ); 
	input.y = glm::abs( glm::floor( pass.view.origin.y * Inv64< float >() ) - glm::ceil( min.y * Inv64< float >() ) ); 
	input.z = glm::abs( glm::floor( pass.view.origin.z * Inv128< float >() ) - glm::ceil( min.z * Inv128< float >() ) ); 
		
	glm::vec3 interp = input / lightvolGrid;

	glm::ivec3 dindex;
	dindex.x = static_cast< int >( interp.x * lightvolGrid.x );
	dindex.y = static_cast< int >( interp.y * lightvolGrid.y );
	dindex.z = static_cast< int >( interp.z * lightvolGrid.z );
	
	// Performs an implicit cast from a vec3 to ivec3
	glm::ivec3 dims( lightvolGrid );

	return ( dindex.z * dims.x * dims.y + dims.x * dindex.y + dindex.x ) % map->data.numLightvols;
}
*/