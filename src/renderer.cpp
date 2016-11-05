#include "renderer.h"
#include "shader.h"
#include "io.h"
#include "lib/math.h"
#include "effect_shader.h"
#include "deform.h"
#include "model.h"
#include "renderer/shader_gen.h"
#include <glm/gtx/string_cast.hpp>
#include <fstream>
#include <random>
#include <algorithm>

struct config_t
{
	bool drawFacesOnly: 1;
	bool drawAtlasTextureBoxes: 1;
	bool logStageTexCoordData: 1;
	bool debugRender;
};

static config_t gConfig =
{
	false,
	false,
	false,
	false
};

struct counts_t
{
	uint32_t numSolidEffect;
	uint32_t numSolidNormal;
	uint32_t numTransEffect;
	uint32_t numTransNormal;
};

static counts_t gCounts = { 0, 0, 0, 0 };
static uint64_t frameCount = 0;

//--------------------------------------------------------------
drawPass_t::drawPass_t( const Q3BspMap& map, const viewParams_t& viewData )
	: isSolid( true ),
	  envmap( false ),
	  faceIndex( 0 ), viewLeafIndex( 0 ),
	  type( PASS_DRAW ), drawType( PASS_DRAW_MAIN ),
	  renderFlags( 0 ),
	  face( nullptr ),
	  leaf( nullptr ),
	  lightvol( nullptr ),
	  shader( nullptr ),
	  view( viewData )
{
	facesVisited.resize( map.data.numFaces, 0 );
}

//--------------------------------------------------------------
BSPRenderer::BSPRenderer( float viewWidth, float viewHeight, Q3BspMap& map_ )
	:	mainSampler( { G_UNSPECIFIED } ),
		glEffects( {
			{
				"tcModTurb",
				[]( const Program& p, const effect_t& e ) -> void
				{
					float turb = DEFORM_CALC_TABLE(
					deformCache.sinTable,
					0,
					e.data.wave.phase,
					GetTimeSeconds(),
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
					p.LoadMat2( "texRotate",
						&e.data.rotation2D.transform[ 0 ][ 0 ] );
					p.LoadVec2( "texCenter", e.data.rotation2D.center );
				}
			}
		} ),
		currLeaf( nullptr ),
		map( map_ ), frustum( new Frustum() ),
		apiHandles( {{ 0, 0 }} ),
		deltaTime( 0.0f ),
		frameTime( 0.0f ),
		alwaysWriteDepth( false ),
		camera( new InputCamera() ),
		curView( VIEW_MAIN )
{
	camera->moveStep = 1.0f;
	camera->SetPerspective( 65.0f, viewWidth, viewHeight, G_STATIC_NEAR_PLANE,
		G_STATIC_FAR_PLANE );
}

BSPRenderer::~BSPRenderer( void )
{
	DeleteBufferObject( GL_ARRAY_BUFFER, apiHandles[ 0 ] );
	DeleteBufferObject( GL_ELEMENT_ARRAY_BUFFER, apiHandles[ 1 ] );
}

void BSPRenderer::MakeProg( const std::string& name, const std::string& vertSrc,
	const std::string& fragSrc, const std::vector< std::string >& uniforms,
	const std::vector< std::string >& attribs )
{
	glPrograms[ name ] = std::unique_ptr< Program >( new Program( vertSrc,
		fragSrc, uniforms, attribs ) );
}

void BSPRenderer::Prep( void )
{
	GEnableDepthBuffer();

	GL_CHECK( glEnable( GL_BLEND ) );

	GL_CHECK( glClearColor( 0.0f, 0.0f, 0.0f, 0.0f ) );

	GL_CHECK( glGenBuffers( apiHandles.size(), &apiHandles[ 0 ] ) );

	// Load main shader glPrograms
	{
		std::vector< std::string > attribs =
		{
			"position",
			"color",
			"tex0",
			"lightmap"
		};

		std::vector< std::string > uniforms =
		{
			"modelToView",
			"viewToClip",

			"mainImageSampler",
			"mainImageImageTransform",
			"mainImageImageScaleRatio",

			"lightmapSampler",
			"lightmapImageTransform",
			"lightmapImageScaleRatio"
		};

		MakeProg( "main", GMakeMainVertexShader(), GMakeMainFragmentShader(),
			uniforms, attribs );
	}
}

bool BSPRenderer::IsTransFace( int32_t faceIndex,
	const shaderInfo_t* shader ) const
{
	UNUSED( shader );
	const bspFace_t* face = &map.data.faces[ faceIndex ];

	if ( face && face->shader != -1 )
	{
		bspShader_t* s = &map.data.shaders[ face->shader ];

		return !!( s->contentsFlags & ( BSP_CONTENTS_WATER
										| BSP_CONTENTS_TRANSLUCENT ) )
			|| !!( s->surfaceFlags & ( BSP_SURFACE_NONSOLID ) );
	}

	return false;
}

void BSPRenderer::LoadPassParams( drawPass_t& p, int32_t face,
	passDrawType_t defaultPass ) const
{
	p.face = &map.data.faces[ face ];
	p.faceIndex = face;
	p.shader = map.GetShaderInfo( face );

	if ( p.shader )
	{
		p.drawType = PASS_DRAW_EFFECT;
	}
	else
	{
		p.drawType = defaultPass;
	}
}

void BSPRenderer::Load( renderPayload_t& payload )
{
	Prep();

	mainSampler = payload.sampler;

	// Create main and shader textures
	{
		gTextureMakeParams_t params( payload.mainImages, mainSampler );
		mainTexHandle = GMakeTexture( params );
	}

	{
		gTextureMakeParams_t params( payload.shaderImages, mainSampler );
		shaderTexHandle = GMakeTexture( params );
	}

	camera->SetViewOrigin( map.GetFirstSpawnPoint().origin );

	GLint oldAlign;
	GL_CHECK( glGetIntegerv( GL_UNPACK_ALIGNMENT, &oldAlign ) );
	GL_CHECK( glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ) );

	LoadLightmaps();

	GL_CHECK( glPixelStorei( GL_UNPACK_ALIGNMENT, oldAlign ) );

	LoadVertexData();

	// Basic program setup
	for ( const auto& iShader: map.effectShaders )
	{
		for ( const shaderStage_t& stage: iShader.second.stageBuffer )
		{
			stage.GetProgram().LoadMat4( "viewToClip",
				camera->ViewData().clipTransform );
		}
	}

	glPrograms[ "main" ]->LoadMat4( "viewToClip",
		camera->ViewData().clipTransform );
}

void BSPRenderer::LoadLightmaps( void )
{
	gImageParamList_t lightmaps;

	// And then generate all of the lightmaps
	for ( int32_t l = 0; l < map.data.numLightmaps; ++l )
	{
		gImageParams_t image;
		image.sampler = mainSampler;
		GSetImageBuffer( image, BSP_LIGHTMAP_WIDTH, BSP_LIGHTMAP_HEIGHT, 255 );

		GSetAlignedImageData( image, &map.data.lightmaps[ l ].map[ 0 ][ 0 ][ 0 ], 3,
			image.width * image.height );

		lightmaps.push_back( image );
	}

	gTextureMakeParams_t makeParams( lightmaps, mainSampler );
	lightmapHandle = GMakeTexture( makeParams );
}

//---------------------------------------------------------------------
// Generate our face/render data
//---------------------------------------------------------------------
void BSPRenderer::LoadVertexData( void )
{
	glFaces.resize( map.data.numFaces );

	if ( gConfig.debugRender )
		glDebugFaces.resize( map.data.numFaces );

	std::vector< bspVertex_t > vertexData( &map.data.vertexes[ 0 ],
		&map.data.vertexes[ map.data.numVertexes ] );

#if !G_STREAM_INDEX_VALUES
	std::vector< uint32_t > indexData;
	MapModelGenIndexBuffer( indexData );
#endif

#if G_STREAM_INDEX_VALUES
	size_t iboSize = 0;
#endif

	// cache the data already used for any polygon or mesh faces, so we don't have to
	// iterate through their index/vertex mapping every frame. For faces
	// which aren't of these two categories, we leave them be.
	for ( int32_t i = 0; i < map.data.numFaces; ++i )
	{
		const bspFace_t* face = &map.data.faces[ i ];

		if ( face->type == BSP_FACE_TYPE_PATCH )
		{
			glFaces[ i ].reset( new mapPatch_t() );
		}
		else
		{
			glFaces[ i ].reset( new mapModel_t() );
		}

		glFaces[ i ]->Generate( vertexData, &map, i );
		glFaces[ i ]->CalcBounds( map.data );

#if G_STREAM_INDEX_VALUES
		// Allocate the largest index buffer out of all models, so we can just
		// stream each item, and save GPU mallocs
		if ( iboSize < glFaces[ i ]->iboRange )
		{
			iboSize = glFaces[ i ]->iboRange;
		}
#endif
		if ( gConfig.debugRender )
		{
			MLOG_ASSERT( false, "gConfig.debugRender is true; you need to add the"\
				" vertex data to the glDebugFaces member" );
			std::random_device r;
			std::default_random_engine e( r() );
			std::uniform_real_distribution< float > urd( 0.0f, 1.0f );

			glm::vec4 color( urd( e ), urd( e ), urd( e ), 1.0f );

			glDebugFaces[ i ].color = color;
		}
	}

	// Allocate vertex data from map and store it all in a single vbo;
	// we use dynamic draw as a hint, considering that vertex deforms
	// require a buffer update
	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, apiHandles[ 0 ] ) );
	GL_CHECK( glBufferData( GL_ARRAY_BUFFER, sizeof( vertexData[ 0 ] )
		* vertexData.size(), &vertexData[ 0 ], GL_DYNAMIC_DRAW ) );

#if !G_STREAM_INDEX_VALUES
	GL_CHECK( glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, apiHandles[ 1 ] ) );
	GL_CHECK( glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( indexData[ 0 ] )
		* indexData.size(), &indexData[ 0 ], GL_STATIC_DRAW ) );
#endif
}

void BSPRenderer::Render( void )
{
	float startTime = GetTimeSeconds();

	if ( map.IsAllocated() )
	{
		RenderPass( camera->ViewData() );
	}

	frameTime = GetTimeSeconds() - startTime;

	frameCount++;
}

void BSPRenderer::DrawDebugFace( uint32_t index )
{
#ifdef EMSCRIPTEN
	UNUSED( index );
#else
	pushBlend_t b( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	const bspFace_t& f = map.data.faces[ index ];

	const glm::mat4& viewM = camera->ViewData().transform;
	const glm::mat4& proj = camera->ViewData().clipTransform;

	if ( f.type != BSP_FACE_TYPE_PATCH )
	{
		glm::vec4 w( 0.3f );

		GU_ImmBegin( GL_TRIANGLES, viewM, proj );
		GU_ImmLoad( glDebugFaces[ index ].positions, w );
		GU_ImmEnd();

		glm::mat4 viewLine( camera->ViewData().transform * glm::translate(
			glm::mat4( 1.0f ), f.lightmapOrigin ) );

		GU_ImmDrawLine( glm::vec3( 0.0f ),
						f.normal * 100.0f,
						w,
						viewLine, proj );
	}
	else
	{
		glPrograms[ "debug" ]->LoadDefaultAttribProfiles();
		glPrograms[ "debug" ]->LoadMat4( "modelToView", viewM );
		glPrograms[ "debug" ]->LoadMat4( "viewToClip", proj );
		glPrograms[ "debug" ]->LoadVec4( "fragColor",
			glDebugFaces[ index ].color );
		glPrograms[ "debug" ]->Bind();
		GU_MultiDrawElements( GL_TRIANGLE_STRIP,
					glFaces[ index ]->ToPatch()->rowIndices,
					glFaces[ index ]->ToPatch()->trisPerRow );
		glPrograms[ "debug" ]->Release();

		glm::mat4 viewLineX( camera->ViewData().transform * glm::translate(
			glm::mat4( 1.0f ), f.lightmapStVecs[ 0 ] ) );
		glm::mat4 viewLineY( camera->ViewData().transform * glm::translate(
			glm::mat4( 1.0f ), f.lightmapStVecs[ 1 ] ) );

		GU_ImmDrawLine( glm::vec3( 0.0f ),
						f.normal * 100.0f,
						glDebugFaces[ index ].color,
						viewLineX, proj );

		GU_ImmDrawLine( glm::vec3( 0.0f ),
						f.normal * 100.0f,
						glDebugFaces[ index ].color,
						viewLineY, proj );
	}
#endif
}

void BSPRenderer::ProcessFace( drawPass_t& pass, uint32_t index )
{
	// if pass.facesVisited[ faceIndex ] is still false after this criteria's
	// evaluations, we'll pick it up on the next pass as it will meet
	// the necessary criteria then.
	if ( pass.facesVisited[ index ] )
	{
		return;
	}

	LoadPassParams( pass, index, PASS_DRAW_MAIN );

	bool transparent = IsTransFace( pass.faceIndex, pass.shader );

	bool add = ( !pass.isSolid && transparent )
		|| ( pass.isSolid && !transparent );

	if ( add )
	{
		if ( gConfig.debugRender )
		{
			DrawDebugFace( index );
			return;
		}

		if ( gConfig.drawFacesOnly )
		{
			DrawFace( pass );
		}
		else
		{
			if ( pass.shader && !!( pass.shader->surfaceParms
				& SURFPARM_NO_DRAW ) )
			{
				pass.facesVisited[ pass.faceIndex ] = true;
				return;
			}

			drawSurfaceList_t& list =
				( pass.face->type == BSP_FACE_TYPE_PATCH )? pass.patches:
				pass.polymeshes;

			AddSurface( pass.shader, pass.faceIndex,
				pass.shader? list.effectSurfaces: list.surfaces );
			pass.facesVisited[ pass.faceIndex ] = true;
		}
	}
}

void BSPRenderer::DrawList( drawSurfaceList_t& list, bool solid )
{
	if ( solid )
	{
		gCounts.numSolidEffect += list.effectSurfaces.size();
		gCounts.numSolidNormal += list.surfaces.size();
	}
	else
	{
		gCounts.numTransEffect += list.effectSurfaces.size();
		gCounts.numTransNormal += list.surfaces.size();
	}

	DrawSurfaceList( list.surfaces, solid );
	DrawSurfaceList( list.effectSurfaces, solid );
	list.surfaces = surfaceContainer_t();
	list.effectSurfaces = surfaceContainer_t();
}

void BSPRenderer::DrawClear( drawPass_t& pass, bool solid )
{
	if ( !gConfig.drawFacesOnly )
	{
		DrawList( pass.polymeshes, solid );
		DrawList( pass.patches, solid );
		pass.shader = nullptr;
		pass.face = nullptr;
	}
}

void BSPRenderer::TraverseDraw( drawPass_t& pass, bool solid )
{
	pass.isSolid = solid;
	DrawNode( pass, 0 );
	DrawClear( pass, solid );
}

void BSPRenderer::RenderPass( const viewParams_t& view )
{
	memset( &gCounts, 0, sizeof( gCounts ) );

	drawPass_t pass( map, view );
	pass.leaf = map.FindClosestLeaf( pass.view.origin );

	frustum->Update( pass.view, true );

	pass.facesVisited.assign( pass.facesVisited.size(), 0 );

	// We start at index 1 because the 0th index
	// provides a model which represents the entire map.
	for ( int32_t i = 1; i < map.data.numModels; ++i )
	{
		bspModel_t* model = &map.data.models[ i ];

		AABB bounds( model->boxMax, model->boxMin );

		if ( !frustum->IntersectsBox( bounds ) )
		{
			continue;
		}

		pass.isSolid = true;
		for ( int32_t j = 0; j < model->numFaces; ++j )
			ProcessFace( pass, model->faceOffset + j );

		pass.isSolid = false;
		for ( int32_t j = 0; j < model->numFaces; ++j )
			ProcessFace( pass, model->faceOffset + j );
	}

	pass.type = PASS_DRAW;

	GL_CHECK( glEnable( GL_CULL_FACE ) );
	GL_CHECK( glCullFace( GL_BACK ) );
	GL_CHECK( glFrontFace( GL_CW ) );

	TraverseDraw( pass, true );
	TraverseDraw( pass, false );

	GL_CHECK( glDisable( GL_CULL_FACE ) );

/*
	MLOG_INFOB( "FPS: %.2f\n numSolidEffect: %i\n numSolidNormal: %i\n numTransEffect: %i\n numTransNormal: %i\n"\
				"Camera Move Speed: %f\n Depth Write ALWAYS Enabled: %s",
				CalcFPS(),
				gCounts.numSolidEffect,
				gCounts.numSolidNormal,
				gCounts.numTransEffect,
				gCounts.numTransNormal,
				camera->moveStep,
				alwaysWriteDepth ? "true": "false" );
				*/
}

void BSPRenderer::Update( float dt )
{
	camera->Update();

	viewParams_t& view = camera->ViewDataMut();
	SetNearFar( view.clipTransform, G_STATIC_NEAR_PLANE, G_STATIC_FAR_PLANE );

	frustum->Update( view, false );

	deltaTime = dt;
}

void BSPRenderer::DrawNode( drawPass_t& pass, int32_t nodeIndex )
{
	if ( nodeIndex < 0 )
	{
		pass.viewLeafIndex = -( nodeIndex + 1 );
		const bspLeaf_t* viewLeaf = &map.data.leaves[ pass.viewLeafIndex ];

		if ( !map.IsClusterVisible( pass.leaf->clusterIndex, viewLeaf->clusterIndex ) )
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

		for ( int32_t i = 0; i < viewLeaf->numLeafFaces; ++i )
			ProcessFace( pass,
						map.data.leafFaces[ viewLeaf->leafFaceOffset + i ].index );
	}
	else
	{
		const bspNode_t* const node = &map.data.nodes[ nodeIndex ];
		const bspPlane_t* const plane = &map.data.planes[ node->plane ];

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

void BSPRenderer::DrawMapPass( int32_t textureIndex, int32_t lightmapIndex,
	std::function< void( const Program& mainRef ) > callback )
{
	GL_CHECK( glDepthFunc( GL_LEQUAL ) );
	GL_CHECK( glBlendFunc( GL_ONE, GL_ZERO ) );

	const Program& main = *( glPrograms.at( "main" ) );

	main.LoadDefaultAttribProfiles();

	gTextureHandle_t mainImageHandle;

	if ( textureIndex == -1 )
	{
		mainImageHandle.id = G_UNSPECIFIED;
	}
	else
	{
		mainImageHandle = mainTexHandle;
	}

	if ( textureIndex == -1 )
	{
		textureIndex = 0;
	}

	GU_SetupTexParams( main, "mainImage", mainImageHandle, textureIndex, 0 );
	GU_SetupTexParams( main, "lightmap", lightmapHandle, lightmapIndex, 1 );

	main.LoadMat4( "modelToView", camera->ViewData().transform );

	main.Bind();

	callback( main );

	main.Release();

	GReleaseTexture( mainTexHandle, 0 );
	GReleaseTexture( lightmapHandle, 1 );
}

namespace {
	INLINE void AddSurfaceData( drawSurface_t& surf, int faceIndex,
		modelBuffer_t& glFaces )
	{
		mapModel_t& model = *( glFaces[ faceIndex ] );

#if G_STREAM_INDEX_VALUES
		surf.drawFaceIndices.push_back( faceIndex );
#else
		if ( surf.faceType == BSP_FACE_TYPE_PATCH )
		{
			mapPatch_t& patch = *( model.ToPatch() );

			surf.bufferOffsets.insert( surf.bufferOffsets.end(),
				patch.rowIndices.begin(), patch.rowIndices.end() );
			surf.bufferRanges.insert( surf.bufferRanges.end(),
				patch.trisPerRow.begin(), patch.trisPerRow.end() );
		}
		else
		{
			surf.bufferOffsets.push_back( model.iboOffset );
			surf.bufferRanges.push_back( model.iboRange );
		}
#endif

		if ( surf.shader && surf.shader->deform )
		{
			surf.faceIndices.push_back( faceIndex );
		}
	}
}

void BSPRenderer::MakeAddSurface( const shaderInfo_t* shader,
	int32_t faceIndex,
	surfaceContainer_t& surfList )
{
	const bspFace_t* face = &map.data.faces[ faceIndex ];

	drawSurface_t surf;

	surf.shader = shader;
	surf.lightmapIndex = face->lightmapIndex;
	surf.textureIndex = face->shader;
	surf.faceType = face->type;
	surf.transparent = IsTransFace( faceIndex, shader );

	AddSurfaceData( surf, faceIndex, glFaces );

	surfList[ face->type - 1 ][ face->lightmapIndex ][ face->shader ]
		[ ( uintptr_t ) shader ] = surf;
}

void BSPRenderer::AddSurface( const shaderInfo_t* shader, int32_t faceIndex,
	surfaceContainer_t& surfList )
{
	const bspFace_t* face = &map.data.faces[ faceIndex ];

	surfMapTier1_t& t1 = surfList[ face->type - 1 ];

	auto t2 = t1.find( face->lightmapIndex );
	if ( t2 != t1.end() )
	{
		auto t3 = t2->second.find( face->shader );
		if ( t3 != t2->second.end() )
		{
			auto surfTest = t3->second.find( ( uintptr_t )shader );

			if  ( surfTest != t3->second.end() )
			{
				AddSurfaceData( surfTest->second, faceIndex, glFaces );
			}
			else
			{
				MakeAddSurface( shader, faceIndex, surfList );
			}
		}
		else
		{
			MakeAddSurface( shader, faceIndex, surfList );
		}
	}
	else
	{
		MakeAddSurface( shader, faceIndex, surfList );
	}
}

void BSPRenderer::DrawSurface( const drawSurface_t& surf ) const
{
	for ( int32_t i: surf.faceIndices )
		DeformVertexes( *( glFaces[ i ] ), surf.shader );

	GLenum mode = ( surf.faceType == BSP_FACE_TYPE_PATCH )? GL_TRIANGLE_STRIP: GL_TRIANGLES;

#if G_STREAM_INDEX_VALUES
	for ( uint32_t i = 0; i < surf.drawFaceIndices.size(); ++i )
	{
		mapModel_t& m = *( glFaces[ surf.drawFaceIndices[ i ] ] );
		GDrawFromIndices( m.indices, mode );
	}
#else
	GU_MultiDrawElements( mode, surf.bufferOffsets, surf.bufferRanges );
#endif
}

void BSPRenderer::DrawEffectPass( const drawTuple_t& data, drawCall_t callback )
{
	const shaderInfo_t* shader = std::get< 1 >( data );
	int lightmapIndex = std::get< 3 >( data );
	bool isSolid = std::get< 4 >( data );

	// Each effect pass is allowed only one texture, so we don't need a second texcoord
	GL_CHECK( glDisableVertexAttribArray( 3 ) );

	// Assess the current culling situation; if the current
	// shader uses a setting which differs from what's currently set,
	// we restore our cull settings to their previous values after this draw
	GLint oldCull = -1, oldCullMode = 0, oldFrontFace = 0;
	if  ( shader->cullFace != G_UNSPECIFIED )
	{
		GL_CHECK( glGetIntegerv( GL_CULL_FACE, &oldCull ) );

		// Store values right now, before the potential change in state
		if ( oldCull )
		{
			GL_CHECK( glGetIntegerv( GL_FRONT_FACE, &oldFrontFace ) );
			GL_CHECK( glGetIntegerv( GL_CULL_FACE_MODE, &oldCullMode ) );
		}

		// Check for desired face culling
		if ( shader->cullFace )
		{
			if ( !oldCull ) // Not enabled, so we need to activate it
				GL_CHECK( glEnable( GL_CULL_FACE ) );

			GL_CHECK( glCullFace( shader->cullFace ) );
		}
		else
		{
			GL_CHECK( glDisable( GL_CULL_FACE ) );
		}
	}

	if ( alwaysWriteDepth )
	{
		GL_CHECK( glDepthMask( GL_TRUE ) );
	}

	for ( int32_t i = 0; i < shader->stageCount; ++i )
	{
		const shaderStage_t& stage = shader->stageBuffer[ i ];
		const Program& stageProg = stage.GetProgram();

		stageProg.LoadMat4( "modelToView", camera->ViewData().transform );

		GL_CHECK( glBlendFunc( stage.blendSrc, stage.blendDest ) );
		GL_CHECK( glDepthFunc( stage.depthFunc ) );

		if ( !alwaysWriteDepth )
		{
			if ( isSolid || ( stage.depthPass && !( stage.blendSrc == GL_ONE && stage.blendDest == GL_ZERO ) ) )
			{
				GL_CHECK( glDepthMask( GL_TRUE ) );
			}
			else
			{
				GL_CHECK( glDepthMask( GL_FALSE ) );
			}
		}

		// TODO: use correct dimensions for texture
		glm::vec2 texDims( 64.0f );

		const gTextureHandle_t& handle = stage.mapType == MAP_TYPE_IMAGE? shaderTexHandle: lightmapHandle;
		const int32_t texIndex = ( stage.mapType == MAP_TYPE_IMAGE )? stage.textureIndex: lightmapIndex;

		GU_SetupTexParams( stageProg, nullptr, handle, texIndex, 0 );

		for ( effect_t e: stage.effects )
		{
			if ( e.name == "tcModScroll" )
			{
				e.data.xyzw[ 2 ] = texDims.x;
				e.data.xyzw[ 3 ] = texDims.y;
			}
			else if ( e.name == "tcModRotate" )
			{
				e.data.rotation2D.center[ 0 ] = 0.5f;
				e.data.rotation2D.center[ 1 ] = 0.5f;
			}

			glEffects.at( e.name )( stageProg, e );
		}

		stageProg.LoadDefaultAttribProfiles();

		stageProg.Bind();
		callback( std::get< 0 >( data ), stageProg, &stage );
		stageProg.Release();

		GReleaseTexture( handle );
	}

	// No need to change state here unless there's the possibility
	// we've modified it
	if ( !alwaysWriteDepth )
	{
		GL_CHECK( glDepthMask( GL_TRUE ) );
	}

	GL_CHECK( glEnableVertexAttribArray( 3 ) );

	// Did we bother checking earlier?
	if ( oldCull != -1 )
	{
		// If true, we had culling enabled previously, so
		// restore previous settings; otherwise, we ensure it's disabled
		if ( oldCull == GL_TRUE )
		{
			GL_CHECK( glEnable( GL_CULL_FACE ) );
			GL_CHECK( glCullFace( oldCullMode ) );
			GL_CHECK( glFrontFace( oldFrontFace ) );
		}
		else
		{
			GL_CHECK( glDisable( GL_CULL_FACE ) );
		}
	}
}

void BSPRenderer::DrawFace( drawPass_t& pass )
{
//	GL_CHECK( glBlendFunc( GL_ONE, GL_ZERO ) );
	//GL_CHECK( glDepthFunc( GL_LEQUAL ) );

	switch ( pass.drawType )
	{
		case PASS_DRAW_EFFECT:
		{
			drawTuple_t data = std::make_tuple( nullptr, pass.shader, pass.face->shader, pass.face->lightmapIndex, pass.isSolid );

			DrawEffectPass( data, [ &pass, this ]( const void* param, const Program& prog, const shaderStage_t* stage )
			{
				UNUSED( param );
				UNUSED( prog );
				DrawFaceVerts( pass, stage );
			});
		}
			break;
		default:
		case PASS_DRAW_MAIN:

			DrawMapPass( pass.face->shader, pass.face->lightmapIndex,
				[ &pass, this ]( const Program& prog )
			{
				UNUSED( prog );
				DrawFaceVerts( pass, nullptr );
			});

			break;
	}

	pass.facesVisited[ pass.faceIndex ] = true;
}

void BSPRenderer::DrawSurfaceList( const surfaceContainer_t& list, bool solid )
{
	UNUSED( solid );

	auto LEffectCallback = [ this ]( const void* voidsurf, const Program& prog,
		const shaderStage_t* stage )
	{
		UNUSED( stage );
		UNUSED( prog );

		const drawSurface_t& surf = *( ( const drawSurface_t* )( voidsurf ) );

		DrawSurface( surf );
	};

	UNUSED( LEffectCallback );

	for ( auto i0 = list.begin(); i0 != list.end(); ++i0 )
	{
		for ( auto i1 = ( *i0 ).begin(); i1 != ( *i0 ).end(); ++i1 )
		{
			for ( auto i2 = i1->second.begin(); i2 != i1->second.end(); ++i2 )
			{
				for ( auto i3 = i2->second.begin(); i3 != i2->second.end();
					++i3 )
				{
					const drawSurface_t& surf = i3->second;

					if ( surf.shader )
					{
						drawTuple_t tuple = std::make_tuple(
							( const void* )&surf,
							surf.shader,
							surf.textureIndex,
							surf.lightmapIndex,
							solid );
						DrawEffectPass( tuple, LEffectCallback );
					}
					else
					{
						DrawMapPass(
							surf.textureIndex,
							surf.lightmapIndex,
							[ &surf, this ]( const Program& main )
							{
								UNUSED( main );
								DrawSurface( surf );
							});
					}
				}
			}
		}
	}
}

void BSPRenderer::DrawFaceVerts( const drawPass_t& pass,
	const shaderStage_t* stage ) const
{
	UNUSED( stage );

	const mapModel_t& m = *( glFaces[ pass.faceIndex ] );

	if ( pass.shader && pass.shader->deform )
	{
		DeformVertexes( m, pass.shader );
	}

	if ( pass.face->type == BSP_FACE_TYPE_POLYGON
		|| pass.face->type == BSP_FACE_TYPE_MESH )
	{
		GU_DrawElements( GL_TRIANGLES, m.iboOffset, m.iboRange );
	}
	else if ( pass.face->type == BSP_FACE_TYPE_PATCH )
	{
		const mapPatch_t& p = *( m.ToPatch() );
		GU_MultiDrawElements( GL_TRIANGLE_STRIP, p.rowIndices, p.trisPerRow );
	}
}

void BSPRenderer::DeformVertexes( const mapModel_t& m,
	const shaderInfo_t* shader ) const
{
	if ( !shader || shader->deformCmd == VERTEXDEFORM_CMD_UNDEFINED ) return;

	/*
	bspVertex_t* vertices;
	gIndex_t* indices;
	GL_CHECK( vertices = ( bspVertex_t* ) glMapBuffer( GL_ARRAY_BUFFER, GL_WRITE_ONLY ) );
	GL_CHECK( indices = ( gIndex_t* ) glMapBuffer( GL_ELEMENT_ARRAY_BUFFER, GL_READ_ONLY ) );

	for ( uint32_t i = 0; i < m.clientVertices.size(); ++i )
	{
		gIndex_t index = indices[ m.iboOffset + i ];

		glm::vec3 position( m.clientVertices[ i ].position );
		glm::vec3 normal( m.clientVertices[ i ].normal );

		normal *= GenDeformScale( m.clientVertices[ i ].position, shader );

		vertices[ index ].position = position + normal;
	}

	GL_CHECK( glUnmapBuffer( GL_ELEMENT_ARRAY_BUFFER ) );
	GL_CHECK( glUnmapBuffer( GL_ARRAY_BUFFER ) );

	*/

	std::vector< bspVertex_t > verts = m.clientVertices;

	for ( uint32_t i = 0; i < verts.size(); ++i )
	{
		glm::vec3 n( verts[ i ].normal * GenDeformScale( verts[ i ].position,
			shader ) );
		verts[ i ].position += n;
	}

	UpdateBufferObject< bspVertex_t >( GL_ARRAY_BUFFER, apiHandles[ 0 ],
		m.vboOffset, verts, false );
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
	const glm::vec3& max = map.data.models[ 0 ].boxMax;
	const glm::vec3& min = map.data.models[ 0 ].boxMin;

	glm::vec3 input;
	input.x = glm::abs( glm::floor( pass.view.origin.x * Inv64< float >() ) - glm::ceil( min.x * Inv64< float >() ) );
	input.y = glm::abs( glm::floor( pass.view.origin.y * Inv64< float >() ) - glm::ceil( min.y * Inv64< float >() ) );
	input.z = glm::abs( glm::floor( pass.view.origin.z * Inv128< float >() ) - glm::ceil( min.z * Inv128< float >() ) );

	glm::vec3 interp = input / lightvolGrid;

	glm::ivec3 dindex;
	dindex.x = static_cast< int32_t >( interp.x * lightvolGrid.x );
	dindex.y = static_cast< int32_t >( interp.y * lightvolGrid.y );
	dindex.z = static_cast< int32_t >( interp.z * lightvolGrid.z );

	// Performs an implicit cast from a vec3 to ivec3
	glm::ivec3 dims( lightvolGrid );

	return ( dindex.z * dims.x * dims.y + dims.x * dindex.y + dindex.x ) % map.data.numLightvols;
}
*/
