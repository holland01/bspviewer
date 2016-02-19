#pragma once

#include "common.h"
#include "renderer_local.h"

#define TEXNAME_CHAR_LIMIT 64
#define G_UNSPECIFIED 0xFFFFFFFF
#define G_INTERNAL_BPP 4 // Just to let everyone know we only care really about RGBA... (most of the time)

#define G_INTERNAL_RGBA_FORMAT GL_RGBA
#define G_RGBA_FORMAT GL_RGBA
#define G_INTERNAL_BYTE_FORMAT GL_ALPHA
#define G_BYTE_FORMAT GL_ALPHA
#define G_MAG_FILTER GL_LINEAR

#define G_MIPMAPPED false

enum
{
	G_TEXTURE_REPEAT = ( 1 << 0 ),
	G_TEXTURE_LINEAR = ( 1 << 1 ),
	G_TEXTURE_FORMAT_ALPHA = ( 1 << 2 ),
	G_TEXTURE_FORMAT_RGBA = ( 1 << 3 )
};

struct gTextureHandle_t
{
	uint32_t id;
};

struct gSamplerHandle_t
{
	uint32_t id;
};

struct gImageParams_t
{
	gSamplerHandle_t sampler;
	int32_t width = 0;
	int32_t height = 0;
	std::vector< uint8_t > data;
};

using gImageParamList_t = std::vector< gImageParams_t >;

struct gTextureImage_t
{
	glm::vec2 stOffsetStart;
	glm::vec2 stOffsetEnd;
	glm::vec2 imageScaleRatio;
	glm::vec2 dims;
};

struct gTextureMakeParams_t
{
	gImageParamList_t& images;
	gImageParamList_t::iterator start;
	gImageParamList_t::iterator end;
	gSamplerHandle_t sampler;

	gTextureMakeParams_t( gImageParamList_t& images_, const gSamplerHandle_t& sampler_ )
		: images( images_ ),
		  start( images_.begin() ),
		  end( images_.end() ),
		  sampler( sampler_ )
	{
	}
};

gSamplerHandle_t GMakeSampler(
	int8_t bpp = G_INTERNAL_BPP,
	bool mipmapped = G_MIPMAPPED,
	uint32_t wrap = GL_CLAMP_TO_EDGE
);

gTextureHandle_t GMakeTexture( gTextureMakeParams_t& makeParams, uint32_t flags );

bool GLoadImageFromFile( const std::string& imagePath, gImageParams_t& image );

bool GDetermineImageFormat( gImageParams_t& image );

bool GSetImageBuffer( gImageParams_t& image, int32_t width, int32_t height, uint8_t fillValue );

void GBindTexture( const gTextureHandle_t& handle, uint32_t offset = 0 );

void GReleaseTexture( const gTextureHandle_t& handle, uint32_t offset = 0 );

const gTextureImage_t& GTextureImage( const gTextureHandle_t& handle, uint32_t slot );

const gTextureImage_t& GTextureImageByKey( const gTextureHandle_t& handle, uint32_t key );

glm::vec2 GTextureInverseRowPitch( const gTextureHandle_t& handle );

void GFreeTexture( gTextureHandle_t& handle );
