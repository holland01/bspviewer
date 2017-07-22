#pragma once

#include "common.h"
#include "bsp_data.h"
#include "renderer/renderer_local.h"
#include <array>

#define BEZ_BUF_COUNT 2
#define BEZ_CONTROL_POINT_COUNT 9

#define DEFORM_TABLE_SIZE 1024
#define DEFORM_TABLE_SIZE_LOG_2 10
#define DEFORM_TABLE_MASK ( DEFORM_TABLE_SIZE - 1 )

// TODO: think about t param and its interaction
// with the deform cache: its passed as a float which
// represents seconds; we may want milliseconds as an integer instead,
// for example
#define DEFORM_CALC_TABLE( table, base, offset, t, f, a ) \
	( ( base ) + ( table )[ int ( ( offset ) + ( t ) * ( f ) * DEFORM_TABLE_SIZE ) & DEFORM_TABLE_MASK ] * ( a ) )

struct bspFace_t;
struct deformModel_t;
struct mapPatch_t;
struct mapData_t;

struct deformGlobal_t
{
	std::array< float, DEFORM_TABLE_SIZE > sinTable;
	std::array< float, DEFORM_TABLE_SIZE > triTable;

	float waveFormScalar;
};

extern deformGlobal_t deformCache;

float GenDeformScale( const glm::vec3& position, const shaderInfo_t* shader );

void GenPatch( gIndexBuffer_t& outIndices, mapPatch_t* model, const shaderInfo_t* shader, int controlPointStart, int indexOffset = 0 );

void TessellateTri(
	std::vector< bspVertex_t >& outVerts,
	std::vector< triangle_t >& triIndices,
	float amount,
	float normalOffsetScale,	// where vertex = vertex + ( normal * normalOffsetScale )
	const bspVertex_t& a,
	const bspVertex_t& b,
	const bspVertex_t& c
);
