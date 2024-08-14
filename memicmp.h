#ifndef MEMICMP_H
#define MEMICMP_H

#include <string.h>

int memicmp( const void *_s1, const void *_s2, size_t len )
{
    const unsigned char	*s1 = (const unsigned char	*)_s1;
    const unsigned char	*s2 = (const unsigned char	*)_s2;
    unsigned char		c1;
    unsigned char		c2;

    for( ;; ) {
        if( len == 0 ) return( 0 );
        c1 = *s1;
        c2 = *s2;
        if( c1 >= 'A' && c1 <= 'Z' ) c1 += 'a' - 'A';
        if( c2 >= 'A' && c2 <= 'Z' ) c2 += 'a' - 'A';
        if( c1 != c2 ) return( *s1 - *s2 );
        ++s1;
        ++s2;
        --len;
    }
}

int _memicmp( const void *_s1, const void *_s2, size_t len )
{
    return( memicmp( _s1, _s2, len ) );
}

#endif
