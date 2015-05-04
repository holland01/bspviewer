#pragma once

#include "common.h"
#include "deform.h"
#include <memory>

/*
=====================================================

Author: Holland Schutte
License: WTFPL

                    q3m.h

        Contains data structure definitions
        for ID's IBSP map format, along with
        a map loader and parser.

        Readers of this file should be familiar with
        documentation on ID's BSP file format if they
        are to understand this.

        Basic documentation can be found at http://www.mralligator.com/q3/

=====================================================
*/




/*
=====================================================

                BSP Map Structs

=====================================================
*/

struct bspEntity_t
{
    char* infoString;
};

struct bspLump_t
{
    int offset;
    int length;
};

struct bspHeader_t
{
    char        id[ 4 ];
    int         version;
    bspLump_t   directories[ 17 ];
};

struct bspPlane_t
{
    glm::vec3       normal;
    float       distance;
};

struct bspNode_t
{
    int     plane;
    int     children[ 2 ];

    glm::ivec3   boxMin;
    glm::ivec3   boxMax;
};

struct bspLeaf_t
{
    int clusterIndex;
    int areaPortal;

    glm::ivec3   boxMin;
    glm::ivec3   boxMax;

    int leafFaceOffset;
    int numLeafFaces;

    int leafBrushOffset;
    int numLeafBrushes;
};

struct bspLeafFace_t
{
    int index;
};

struct bspLeafBrush_t
{
	int index;
};

struct bspModel_t
{
    glm::vec3 boxMax;
    glm::vec3 boxMin;

    int faceOffset;
    int numFaces;

    int brushOffset;
    int numBrushes;
};

struct bspBrush_t
{
	int brushSide;
	int numBrushSides;
	int texture;
};

struct bspBrushSide_t
{
	int plane;
	int texture;
};

struct bspVertex_t
{
    glm::vec3 position;
    glm::vec2 texCoords[ 2 ]; // 0 => surface, 1 => lightmap
    glm::vec3 normal;

    glm::u8vec4 color;

	bspVertex_t( void );
	bspVertex_t( const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& surfTexCoords, const glm::vec2& lightmapTexCoords, const glm::u8vec4& colors );
	bspVertex_t( const bspVertex_t& v );
	bspVertex_t& operator=( bspVertex_t v );
};

bspVertex_t& operator += ( bspVertex_t& a, const bspVertex_t& b );

bspVertex_t operator +( const bspVertex_t& a, const bspVertex_t& b );
bspVertex_t operator -( const bspVertex_t& a, const bspVertex_t& b );
bspVertex_t operator *( const bspVertex_t& a, float b );

bool operator == ( const bspVertex_t&a, const bspVertex_t& b );

struct bspTexture_t
{
    char    name[ 64 ];
    int     surfaceFlags;
    int     contentsFlags;
};

struct bspMeshVertex_t
{
    int offset;
};

struct bspEffect_t
{
    char    name[ 64 ];
    int     brush;
    int     unknown;
};

struct bspFace_t
{
    int texture;
    int effect;
    int type;

    int vertexOffset;
    int numVertexes;

    int meshVertexOffset;
    int numMeshVertexes;

    int lightmapIndex;
    int lightmapStartCorner[ 2 ];
    int lightmapSize[ 2 ];

    glm::vec3 lightmapOrigin; // in world space
    glm::vec3 lightmapStVecs[ 2 ]; // world space s/t unit vectors
    glm::vec3 normal;

    int     size[ 2 ];
};

struct bspLightmap_t
{
	byte map[ BSP_LIGHTMAP_WIDTH ][ BSP_LIGHTMAP_HEIGHT ][ 3 ]; // lightmap color data. RGB.
};

struct bspLightvol_t
{
	byte ambient[ 3 ];		// RGB color
	byte directional[ 3 ];	// RGB color
	byte direction[ 2 ];	// - to light; 0 => phi, 1 => theta
};

struct bspVisdata_t
{
    int     numVectors;
    int     sizeVector;

    byte*   bitsets;
};

/*
=====================================================

        Q3BspParser: map loader and parser.

=====================================================
*/

struct shaderInfo_t;

struct mapData_t
{
	byte*				buffer;  // all file memory comes from this

	bspHeader_t*		header;

	bspEntity_t         entities;

    bspNode_t*          nodes;

    bspLeaf_t*          leaves;
	bspLeafBrush_t*		leafBrushes;
	bspLeafFace_t*      leafFaces;

    bspPlane_t*         planes;
	bspVertex_t*        vertexes;

	bspBrush_t*			brushes;
	bspBrushSide_t*		brushSides;

    bspTexture_t*       textures;
	bspModel_t*         models;
 	
	bspEffect_t*		effectShaders;
    bspFace_t*          faces;
	
    bspMeshVertex_t*    meshVertexes;

	bspLightmap_t*		lightmaps;
	bspLightvol_t*		lightvols;

    bspVisdata_t*       visdata;

	int                 entityStringLen;
    int                 numEffectShaders;

    int                 numNodes;

    int                 numLeaves;
	int                 numLeafFaces;
	int					numLeafBrushes;

    int                 numPlanes;

    int                 numVertexes;

	int					numBrushes;
	int					numBrushSides;

    int                 numTextures;
    int                 numModels;

	int					numEffects;
    int                 numFaces;

    int                 numMeshVertexes;

	int					numLightmaps;
	int					numLightvols;

    int                 numVisdataVecs;

	std::string			basePath; // root directory of the map
};

class Q3BspMap
{
private:

    Q3BspMap( const Q3BspMap& ) = delete;
    Q3BspMap& operator=( Q3BspMap ) = delete;

    bool							mapAllocated;

	void							ReadFile( const std::string& filepath, const int scale );
	
	void							GenNonShaderTextures( uint32_t loadFlags );
	
	void							GenRenderData( void );

public:

	std::vector< GLuint >			glTextures;		// has one->one map with texture indices
	std::vector< GLuint >			glSamplers;		// has one->one map with glTextures
	std::vector< GLuint >			glLightmaps;	// textures - has one->one map with lightmap indices
	std::vector< mapModel_t >		glFaces;		// has one->one map with face indices
					GLuint			glLightmapSampler;

	std::map< std::string, shaderInfo_t > effectShaders;

    Q3BspMap( void );
    ~Q3BspMap( void );

	mapData_t					data;

    void						Read( const std::string& filepath, const int scale, uint32_t loadFlags );

    void						SetVertexColorIf( bool ( predicate )( unsigned char* ), const glm::u8vec3& rgbColor );

    bspLeaf_t*					FindClosestLeaf( const glm::vec3& camPos );

    bool						IsClusterVisible( int sourceCluster, int testCluster );

    bool						IsAllocated( void ) const { return mapAllocated; }

	const shaderInfo_t*			GetShaderInfo( int faceIndex ) const;

    void						DestroyMap( void );
};
