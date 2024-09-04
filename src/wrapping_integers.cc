#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.

  return Wrap32( static_cast<uint32_t>( n ) + zero_point.raw_value_ );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  const uint64_t n_low32 { this->raw_value_ - zero_point.raw_value_ };
  const uint64_t c_low32 { checkpoint & MASK_LOW_32 };
  const uint64_t res { ( checkpoint & MASK_HIGH_32 ) | n_low32 };
  if ( res >= BASE and n_low32 > c_low32 and ( n_low32 - c_low32 ) > ( BASE / 2 ) ) {
    return res - BASE;
  }
  if ( res < MASK_HIGH_32 and c_low32 > n_low32 and ( c_low32 - n_low32 ) > ( BASE / 2 ) ) {
    return res + BASE;
  }
  return res;
}
