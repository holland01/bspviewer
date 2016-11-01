
#include "atlas_gen.h"
#include "io.h"
#include "stats.h"
#include "renderer/texture.h"
#include "math.h"
#include <memory>
#include <utility>

struct bounds_t
{
	uint16_t dimX, dimY;
	glm::vec2 origin;
};

struct atlasBucket_t
{
	uint16_t count = 0;
	uint16_t val = 0;
	std::unique_ptr< atlasBucket_t > next;

	void WriteCount( uint16_t c )
	{
		count = c | ( c << 8 );
	}

	void IncCount( void )
	{
		uint16_t c = count & 0xFF;
		c++;
		WriteCount( c );
	}

	uint8_t ReadCount( void ) const
	{
		return ( uint8_t )( count & 0xFF );
	}

	void SubOffset( void )
	{
		uint16_t off = ( count >> 8 ) & 0xFF;
		off--;
		count = ( count & 0xFF ) | ( off << 8 );
	}

	uint8_t ReadOffset( void ) const
	{
		return ( uint8_t ) ( ( count >> 8 ) & 0xFF );
	}

	uint16_t TotalBuckets( void ) const
	{
		uint16_t thisCount = ReadCount();

		if ( next )
		{
			thisCount += next->TotalBuckets();
		}

		return thisCount;
	}

	std::string Info( void ) const
	{
		std::stringstream ss;
		uint32_t c = ReadCount();
		ss << "Height: " << val <<  "\nCount: " << c << "\n";

		if ( next )
		{
			ss << next->Info();
		}

		std::string result ( ss.str() );

		return result;
	}

	// Grab the height group with the range of indices containing
	// the given offset index
	atlasBucket_t* FindRange( uint16_t offset, atlasBucket_t** prev,
		uint16_t i = 0 )
	{
		uint32_t upperBound = i + ReadCount();

		if ( i <= offset )
		{
			if ( next )
			{
				if ( offset < upperBound )
				{
					return this;
				}

				*prev = this;

				return next->FindRange( offset, prev, upperBound );
			}

			return this;
		}
		else if ( next )
		{
			*prev = this;

			return next->FindRange( offset, prev, upperBound );
		}

		return nullptr;
	}
};

struct atlasTree_t
{
	uint16_t key;
	std::unique_ptr< atlasTree_t > left;
	std::unique_ptr< atlasTree_t > right;

	std::unordered_map< uint16_t, bool > used;

	std::vector< std::unique_ptr< atlasBucket_t > > columns;

	std::unique_ptr< atlasBucket_t >& First( void )
	{
		return columns[ 0 ];
	}
};

struct slotMetrics_t
{
	uint16_t width;
	uint16_t numBuckets;
};

struct atlasTreeMetrics_t
{
	slotMetrics_t highest;		// the width "slot" which holds the maximum height value
	slotMetrics_t nextHighest;	// same thing, but the second largest
	uint16_t base;				// each width category, summed
	stats_t< uint16_t > bucketCounts;
};

namespace {

struct meta_t
{
	LogHandle log;

	meta_t( void )
		: log( "log/atlas_gen.txt", true )
	{
	}

	void LogData( const atlasTreeMetrics_t& metrics, atlasTree_t& treeRoot )
	{
		O_LogF( log.ptr, "METRICS", "\n\thighest: %i\n\tnextHighest: %i\n"\
				"\tbase: %i\n\n\n\n",
				metrics.highest, metrics.nextHighest, metrics.base );

		LogData_r( &treeRoot );
	}

	void LogData_r( atlasTree_t* t )
	{
		if ( t )
		{
			LogData_r( t->left.get() );

			if ( t->columns.empty() )
			{
				O_LogF( log.ptr, "entry",
					"\nWidth: %i\n Bucket (Column) Count: %i\n"\
					" ( No Buckets/Columns )", t->key, t->columns.size() );
			}
			else
			{
				std::string info( t->First()->Info() );
				O_LogF( log.ptr, "entry", "\nWidth: %i\n"\
					" Bucket (Column) Count: %i\n Bucket Info: \n\n%s\n",
					t->key, t->columns.size(), info.c_str() );
			}

			LogData_r( t->right.get() );
		}
	}
};

std::unique_ptr< meta_t > gMeta( nullptr );

void ShiftForward( std::unique_ptr< atlasBucket_t >& newb, atlasBucket_t* p,
	uint16_t v )
{
	newb->val = p->val;
	newb->count = p->count;
	newb->next = std::move( p->next );
	p->next = std::move( newb );
	p->val = v;
	p->WriteCount( 1 );
}

// Insert height values in descending order
// ( largest value is always first )
void BucketInsert( atlasTree_t& t, uint16_t v )
{
	t.used[ v ] = false;

	if ( t.columns.empty() )
	{
		std::unique_ptr< atlasBucket_t > f( new atlasBucket_t() );
		f->val = v;
		f->count = 1;
		t.columns.push_back( std::move( f ) );
		return;
	}

	atlasBucket_t* prev, *p;

	prev = nullptr;
	p = t.First().get();

	while ( p && v < p->val )
	{
		prev = p;
		p = p->next.get();
	}

	if ( p && p->val == v )
	{
		p->IncCount();
		return;
	}

	// v < prev->val
	if ( prev )
	{
		std::unique_ptr< atlasBucket_t > newb( new atlasBucket_t() );

		// p->val < v < prev->val, so insert newb between
		// p and prev
		if ( p )
		{
			ShiftForward( newb, p, v );
		}
		else
		{
			newb->val = v;
			newb->WriteCount( 1 );
			prev->next = std::move( newb );
		}
	}
	// if prev == nullptr, p automatically isn't null
	// and v > first element, so we just correct that.
	else
	{
		std::unique_ptr< atlasBucket_t > newb( new atlasBucket_t() );
		ShiftForward( newb, p, v );
	}
}

std::unique_ptr< atlasTree_t > TreeMake( uint16_t k, uint16_t v )
{
	std::unique_ptr< atlasTree_t > t( new atlasTree_t() );
	t->key = k;

	std::unique_ptr< atlasBucket_t > buck( new atlasBucket_t() );

	buck->val = v;
	buck->WriteCount( 1 );

	t->columns.push_back( std::move( buck ) );

	return t;
}

void TreeInsert( atlasTree_t& t, uint16_t k, uint16_t v );

void InsertOrMake( std::unique_ptr< atlasTree_t >& t, uint16_t k,
	uint16_t v )
{
	if ( t )
	{
		TreeInsert( *t, k, v );
	}
	else
	{
		t = TreeMake( k, v );
	}
}

void TreeInsert( atlasTree_t& t, uint16_t k, uint16_t v )
{
	if ( k < t.key )
	{
		InsertOrMake( t.left, k, v );
	}
	else if ( k > t.key )
	{
		InsertOrMake( t.right, k, v );
	}
	else
	{
		BucketInsert( t, v );
	}
}

bool TraverseColumn( atlasTree_t& t, atlasPositionMap_t& map, uint8_t index )
{
	map.origin.y = 0;

	atlasBucket_t* curr = t.columns[ index ].get();

	// offset our origin through the summation of all heights which
	// are placed before the height section the owning image belongs to;
	// take into account the amount of duplications for every height value
	while ( curr && map.image->height < curr->val )
	{
		map.origin.y += ( float )curr->val * ( float )curr->ReadCount();
		curr = curr->next.get();
	}

	// if curr != nullptr, we know that, by nature of the this structure,
	// curr->val is implicitly == image.height
	if ( curr )
	{
		if ( curr->ReadOffset() >= 1 )
		{
			curr->SubOffset();
			map.origin.y += ( float )curr->val * ( float )curr->ReadOffset();
		}
		else if ( t.used[ curr->val ] )
		{
			return false;
		}
		else
		{
			t.used[ curr->val ] = true;
		}
	}

	return !!curr;
}

uint16_t SumBounds( const atlasTree_t* t, uint16_t target )
{
	uint16_t s = 0;

	if ( t )
	{
		s += SumBounds( t->left.get(), target );

		if ( t->key < target )
		{
			s += t->key * t->columns.size();
			s += SumBounds( t->right.get(), target );
		}
	}

	return s;
}

void CalcMetrics( atlasTree_t* t, atlasTreeMetrics_t& metrics )
{

	// TODO: make this in order; it's actually more efficient that way,
	// given the InsertOrdered() call
	if ( t )
	{
		CalcMetrics( t->left.get(), metrics );
		CalcMetrics( t->right.get(), metrics );

		// TODO: iterate over all of the root buckets
		// within the tree; this is useful if we want to
		// perform more analytics later (i.e., after more columns
		// have been potentially added)
		if ( !t->columns.empty() )
		{
			uint16_t totalBuckets = t->First()->TotalBuckets();

			if ( totalBuckets > metrics.highest.numBuckets )
			{
				metrics.highest.width = t->key;
				metrics.highest.numBuckets = totalBuckets;
			}
			else if ( totalBuckets > metrics.nextHighest.numBuckets )
			{
				metrics.nextHighest.width = t->key;
				metrics.nextHighest.numBuckets = totalBuckets;
			}

			metrics.bucketCounts.InsertOrdered( totalBuckets );
		}
	}
}

// The ReadCount and ReadOffset methods will initially return the same values.
// Things change, however, when the image's height corresponds to a particular bucket:
// the offset portion will decrement for each image which has a matching height value.
// The initial count bits will remain the same, to ensure that images of different
// heights will not "invade" the space of the given image these parameters correspond to.
void TreePoint( atlasTree_t* t, atlasPositionMap_t& map,
	const atlasTree_t* root )
{
	if ( t )
	{
		if ( map.image->width < t->key )
		{
			TreePoint( t->left.get(), map, root );
		}
		else if ( map.image->width > t->key )
		{
			TreePoint( t->right.get(), map, root );
		}
		else
		{
			volatile bool found = false;

			uint32_t i;

			for ( i = 0; i < t->columns.size() && !found; )
			{
				found = TraverseColumn( *t, map, i );

				if ( !found )
				{
					++i;
				}
			}

			assert( found );

			map.origin.x = SumBounds( root, t->key ) + i * t->key;
		}
	}
}

atlasTree_t* TreeFetch( atlasTree_t* t, uint16_t key )
{
	if ( t )
	{
		if ( key < t->key )
		{
			return TreeFetch( t->left.get(), key );
		}
		else if ( key > t->key )
		{
			return TreeFetch( t->right.get(), key );
		}
	}

	return t;
}

void DuplicateColumn( atlasTree_t& dest, const atlasBucket_t* src,
	uint16_t count )
{
	if ( !count )
	{
		return;
	}

	std::unique_ptr< atlasBucket_t > nextCol( new atlasBucket_t() );
	nextCol->val = src->val;
	nextCol->WriteCount( count );
	dest.columns.push_back( std::move( nextCol ) );
}

uint16_t CalcWidth( atlasTree_t* t )
{
	uint16_t w = 0;

	if ( t )
	{
		w += CalcWidth( t->left.get() );
		w += t->key * t->columns.size();
		w += CalcWidth( t->right.get() );
	}

	return w;
}

uint16_t CalcHeight( const atlasTree_t* t )
{
	uint16_t h0, h1, h2;

	h0 = h1 = h2 = 0;

	if ( t )
	{
		h0 = CalcHeight( t->left.get() );

		for ( const std::unique_ptr< atlasBucket_t >& b: t->columns )
		{
			uint16_t m = 0;
			const atlasBucket_t* p = b.get();

			while ( p )
			{
				m += p->val * p->ReadCount();
				p = p->next.get();
			}

			if ( h1 < m )
			{
				h1 = m;
			}
		}

		h2 = CalcHeight( t->right.get() );
	}

	return std::max( h0, std::max( h1, h2 ) );
}

INLINE bool ValidateDims( uint16_t width, uint16_t height,
	uint16_t maxTextureSize )
{
	bool good = width < maxTextureSize && height < maxTextureSize;

	if ( !good )
	{
		MLOG_ERROR(
			 "Width and Height exceed max GL texture size."\
			 " (GL Max, width, height) => (%iu, %iu, %iu)",
			 maxTextureSize, width, height );
	}

	return good;
}

// AtlasGenVariedOrigins is used to produce a texture atlas from a
// list of images which have varying sizes. Most image groups for the
// BSP Renderer fall into this category.

//-----------------
// Tree Generation
//-----------------

// The algorithm begins by treating each image width as a unique node in a BST.
// The node holds "buckets" of sorted height values corresponding
// to images which have the same width value.

// Each bucket holds a count for each image with that very width and height
// combination. So, if we have N images with a width of 256 containing M
// buckets, and P of these N images holds a height of 128, there will be one of
// these M buckets which is used to represent the height of 128 with a count of
// P. No other bucket in the subtree of width 256 will contain the same height
// value.

// An ordered set of (unique) width values is first constructed; the root
// node in the tree uses the median of these values. In the event that the
// count of width sizes is even, the root node _doesn't_ hold any actual
// buckets, since its value is ( a + b ) / 2, where a and b represent the
// two middle-most values in the set. This doesn't really cause any problems,
// though.

//----------------------------
// NOTE: the tree would probably benefit from using auto-balancing
// techniques to speed up the generation: this would guarantee
// logarithmic traversal.
//----------------------------

// Once each node and its corresponding buckets have been generated, the
// next step is to ensure that we can actually use this layout as an actual
// texture atlas.

//-----------------
// Initial Layout
//-----------------

// Each node contributes to the the atlas's dimensions via its width: the total
// summation of widths makes up the actual base of the atlas itself,
// or the atlas's total width.

// The width area which contains the buckets whose stacked height is the max-
// imum out of each possible node width is what decides the total height
// of the atlas. Since there are buckets which represent more than a single
// image, each bucket's contribution towards this value takes into account
// the amount of image's it represents like so:

// totalHeight = 0
// for i = 0; i < node.buckets.size(); i += 1 {
//	totalHeight += node.buckets[ i ].height * node.buckets[ i ].count
// }

// The atlas maintains the property that every height value
// is stored in a node's bucket list in descending order; the
// first bucket always holds the highest value. Thus, the lower
// left-most image will always be the smallest, while the upper right-most
// image will always be the largest.

// This means the end result is the by-product of a simple and intuitive means
// for generation. Furthermore, this allows us to stack buckets which store the
// same height values linearly via a small offset value that can be incremented
// or decremented as necessary.

//-----------------
// Layout Fitting
//-----------------

// The columns member for each tree node represents the amount of duplication
// necessary to ease the height of the initial layout. Initially, each
// column has one member: if an adjustment needs to be made, another column
// with is created for that particular width, using the bucket with the most
// counts as its initial member.

std::vector< atlasPositionMap_t > AtlasGenVariedOrigins(
		const std::vector< gImageParams_t >& params,
		uint16_t maxTextureSize )
{
	atlasTree_t rootTree;
	stats_t< uint16_t > widths;
	uint16_t width, height;

	for ( uint16_t i = 0; i < params.size(); ++i )
	{
		widths.InsertOrderedUnique( params[ i ].width );
	}

	rootTree.key = widths.GetMedian();

	for ( const gImageParams_t& param: params )
	{
		TreeInsert( rootTree, param.width, param.height );
	}

	atlasTreeMetrics_t metrics =
	{
		{ 0, 0 },
		{ 0, 0 },
		0,
		{}
	};

	std::vector< atlasPositionMap_t > posMap;

	metrics.base = widths.Sum();

	CalcMetrics( &rootTree, metrics );

	// Check to see if our highest bucket count is two standard deviations
	// from the nextHighest. If so, we should split any bucket groups which
	// are too high into separate columns: this should
	// alleviate any potential problems with attempting a texture allocation
	// which is larger than GL_MAX_TEXTURE_SIZE
	float stdDev, zHigh;
	zHigh = metrics.bucketCounts.ZScore( metrics.highest.numBuckets, &stdDev );

	if ( 2.0f <= zHigh )
	{
		atlasTree_t* t = TreeFetch( &rootTree, metrics.highest.width );

		atlasBucket_t* prevHigh = nullptr;
		atlasBucket_t* high = nullptr;

		atlasBucket_t* p = nullptr;
		atlasBucket_t* b = t->First().get();

		uint16_t newCount, counter, rem;

		while ( b )
		{
			if ( ( high && b->ReadCount() > high->ReadCount() ) || !high )
			{
				prevHigh = p;
				high = b;
			}

			p = b;
			b = b->next.get();
		}

		uint16_t subDivisions = 1;

		// FIXME: What about the case in which prevHigh doesn't exist
		// because every ReadCount() check in the loop fails, but
		// the actual bucket count > 1? Obviously if there's only
		// one bucket count then separation makes no sense...

		// prevHigh->next = high, obviously
		if ( prevHigh )
		{
			t->columns.push_back( std::move( prevHigh->next ) );
			prevHigh->next.reset( nullptr );
		}
		// We produce more subdivisions because a lack of a prevHigh
		// implies that our max bucket dominates in size more than
		// it would otherwise
		else
		{
			subDivisions++;
		}

		counter = newCount = high->ReadCount() >> subDivisions;
		rem = high->ReadCount() % ( 1 << subDivisions );

		while ( counter < high->ReadCount() )
		{
			DuplicateColumn( *t, high, newCount );
			counter += newCount;
		}

		high->WriteCount( newCount );
		DuplicateColumn( *t, high, rem );
	}

	width = CalcWidth( &rootTree );
	height = CalcHeight( &rootTree );

	if ( !ValidateDims( width, height, maxTextureSize ) )
	{
		return posMap;
	}

	// Query the max value and send it in

	for ( const gImageParams_t& image: params )
	{
		atlasPositionMap_t pmap;
		pmap.image = &image;
		TreePoint( &rootTree, pmap, &rootTree );
		posMap.push_back( pmap );
	}

	if ( gMeta )
	{
		gMeta->LogData( metrics, rootTree );
	}

	return posMap;
}

// For lists of images which all have the same dimensions
std::vector< atlasPositionMap_t > AtlasGenUniformOrigins(
	const std::vector< gImageParams_t >& params, uint16_t maxTextureSize )
{
	std::vector< atlasPositionMap_t > posMap;
	uint16_t square, width, height;

	square = NextSquare( params.size() );
	width = NextPower2( square * params[ 0 ].width );
	height = NextPower2( square * params[ 0 ].height );

	if ( !ValidateDims( width, height, maxTextureSize ) )
	{
		return posMap;
	}

	for ( uint16_t y = 0; y < square; ++y )
	{
		for ( uint16_t x = 0; x < square; ++x )
		{
			uint16_t slot = y * square + x;

			if ( slot >= params.size() )
			{
				break;
			}

			atlasPositionMap_t map;
			map.image = &params[ slot ];
			map.origin = glm::vec2( x * params[ slot ].width,
				y * params[ slot ].height );
			posMap.push_back( map );
		}
	}

	return posMap;
}

} // end namespace

std::vector< atlasPositionMap_t > AtlasGenOrigins(
		const std::vector< gImageParams_t >& params,
		uint16_t maxTextureSize )
{
#ifdef LOG_ATLAS_GEN
	if ( !gMeta )
	{
		gMeta.reset( new meta_t() );
	}
#endif

	// Determine our atlas layout
	for ( uint16_t i = 1; i < params.size(); ++i )
	{
		// If true, we know that there is at least one image with differing
		// dimensions from the rest, so we take that into account...
		if ( params[ i - 1 ].width != params[ i ].width
			 || params[ i - 1 ].height != params[ i ].height )
		{
			return AtlasGenVariedOrigins( params, maxTextureSize );
		}
	}

	return AtlasGenUniformOrigins( params, maxTextureSize );
}
