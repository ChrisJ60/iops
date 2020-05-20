#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>

#define M_KB      1024LL
#define M_MB      1048576LL
#define M_GB      1073741824LL
#define M_TB      1099511627776LL
#define M_PB      1125899906842624LL

void
usage( void )
{
    printf("\nUsage:\n\n");

    printf("    rawsz /dev/rawdevicename\n\n");

    printf("Displays the size of the specified raw device in bytes\n\n");

    exit( 100 );
}

off_t
alignOffset( off_t offset, long blksz )
{
    off_t rem;

    rem = offset % blksz;
    if (  rem  )
    {
        offset -= rem;
        if ( offset < 0  )
            offset = 0;
    }

    return offset;
}

int
probeBlock( int fd, off_t offset, long blksz, void * blk )
{
    off_t offret = 0;
    ssize_t nbytes;

    errno = 0;
    if (  lseek( fd, offset, SEEK_SET ) != offset  )
        return 0;
    errno = 0;
    if (  read( fd, blk, blksz ) != blksz  )
        return 0;

    return 1;
}

long long
findSize( int fd, off_t maxsz, long blksz, void * ioblk )
{
    off_t poffset;
    off_t offset;
    off_t minsz;

    minsz = 0;
    if (  ! probeBlock( fd, minsz, blksz, ioblk )  )
        return 0;

    offset = alignOffset( maxsz, blksz );
    if (  probeBlock( fd, offset, blksz, ioblk )  )
        return maxsz;

    do {
        poffset = offset;;
        offset = alignOffset( ( (maxsz - minsz) / 2 ) + minsz, blksz );
        if (  probeBlock( fd, offset, blksz, ioblk )  )
            minsz = offset;
        else
            maxsz = offset;
    } while ( offset != poffset );
    
    return ( offset + blksz );
}

int
main( int argc, char *argv[] )
{
    int fd = -1;
    long long fsize;
    struct stat sb;
    long blksz;
    void * ioblk = NULL;

    setlocale( LC_ALL, "" );

    if (  argc != 2  )
        usage();

    errno = 0;
    fd = open( argv[1], O_RDONLY );
    if (  fd < 0  )
    {
        fprintf( stderr, "error: unable to open '%s' - %d (%s)\n", argv[1], errno, strerror(errno) );
        return 1;
    }

    errno = 0;
    if (  fstat( fd, &sb )  )
    {
        fprintf( stderr, "error: stat() failed for '%s' - %d (%s)\n", argv[1], errno, strerror(errno) );
        return 2;
    }

    if (  ! ( sb.st_mode & S_IFCHR ) && ! ( sb.st_mode & S_IFBLK )  )
    {
        fprintf( stderr, "error: '%s' is not a raw/block device\n", argv[1] );
        return 3;
    }
    blksz = sb.st_blksize;

    ioblk = (void *)valloc( blksz );
    if (  ioblk == NULL  )
    {
        fprintf( stderr, "error: unable to allocate %'ld bytes\n", blksz );
        return 4;
    }

    fsize = findSize( fd, M_PB, blksz, ioblk );

    close( fd );

    printf("%'lld\n", fsize );

    return 0;
}
