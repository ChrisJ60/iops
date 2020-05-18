/**********************************************************************
 *
 * Filesystem information utility (statfs)
 *
 * Copyright (c) Chris Jenkins 2019, 2020
 *
 * Licensed under the Universal Permissive License v 1.0 as shown
 * at http://oss.oracle.com/licenses/upl
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#if defined(LINUX)
#include <sys/vfs.h>
#define __USE_GNU 1
#include <sys/statvfs.h>
#else /* ! LINUX */
#include <sys/param.h>
#include <sys/mount.h>
#endif /* ! LINUX */

#define   FMT_RAW      0
#define   FMT_SHELL    1
#define   FMT_HUMAN    2

#define   FSOPTLEN     20
#define   NUMFSOPT     21

#define   USER_UNKNOWN "*unknown*"
#define   RPT_DIVIDER  "----------------------------------------"

void
usage( void )
{
    printf("\nUsage:\n\n");

#if ! defined(LINUX)
    printf("    statfs [-s | -r] [path]\n\n");

    printf("If 'path' is provided, displays filesystem information for the\n");
    printf("specified path otherwise displays filesystem information for all\n");
    printf("mounted filesystems.\n\n");
#else /* LINUX */
    printf("    statfs [-s | -r] path\n\n");

    printf("Displays filesystem information for the specified path.\n\n");
#endif /* LINUX */

    printf("By default the information is displayed in a human friendly format.\n");
    printf("If '-r' is specified the information is displayed in raw format in\n");
    printf("the same order as the as the definition of the 'statfs' structure.\n");
    printf("If '-s' is specified then the output is in 'shell' format suitable\n");
    printf("for use with 'eval'.\n\n");

    exit( 100 );
} // usage

char *
strUpper( char * s )
{
    char * p = s;

    if (  s != NULL  )
        while (  *p  )
        {
            *p = toupper( *p );
            p += 1;
        }

    return s;
} // strUpper

char *
getUserName( uid_t uid )
{
    char * uname = NULL;
    struct passwd * pwent = NULL;

    pwent = getpwuid( uid );
    if (  pwent == NULL  )
        uname = strdup( USER_UNKNOWN );
    else
        uname = strdup( pwent->pw_name );
    
    return uname;
} // getUserName

#if ! defined(LINUX)
void
decodeFlag( unsigned int * flags, unsigned int fval, char * fname, char ** buf )
{
    if (  *flags & fval )
    {
        (*buf) += sprintf( *buf, "%s,", fname );
        *flags &= ~fval;
    }
} // decodeFlag

char *
decodeFsFlags( unsigned int flags )
{
static char optbuf[(FSOPTLEN*NUMFSOPT)+1];
    char * p = optbuf;

    memset( optbuf, 0, sizeof(optbuf) );

    decodeFlag( &flags, MNT_RDONLY, "RDONLY", &p );
    decodeFlag( &flags, MNT_SYNCHRONOUS, "SYNCHRONOUS", &p );
    decodeFlag( &flags, MNT_NOEXEC, "NOEXEC", &p );
    decodeFlag( &flags, MNT_NOSUID, "NOSUID", &p );
    decodeFlag( &flags, MNT_NODEV, "NODEV", &p );
    decodeFlag( &flags, MNT_UNION, "UNION", &p );
    decodeFlag( &flags, MNT_ASYNC, "ASYNC", &p );
    decodeFlag( &flags, MNT_EXPORTED, "EXPORTED", &p );
    decodeFlag( &flags, MNT_REMOVABLE, "REMOVABLE", &p );
    decodeFlag( &flags, MNT_QUARANTINE, "QUARANTINE", &p );
    decodeFlag( &flags, MNT_LOCAL, "LOCAL", &p );
    decodeFlag( &flags, MNT_QUOTA, "QUOTA", &p );
    decodeFlag( &flags, MNT_ROOTFS, "ROOTFS", &p );
    decodeFlag( &flags, MNT_DOVOLFS, "DOVOLFS", &p );
    decodeFlag( &flags, MNT_DONTBROWSE, "DONTBROWSE", &p );
    decodeFlag( &flags, MNT_IGNORE_OWNERSHIP, "IGNOREOWNERS", &p );
    decodeFlag( &flags, MNT_AUTOMOUNTED, "AUTOMOUNTED", &p );
    decodeFlag( &flags, MNT_JOURNALED, "JOURNALED", &p );
    decodeFlag( &flags, MNT_DEFWRITE, "DEFWRITE", &p );
    decodeFlag( &flags, MNT_MULTILABEL, "MULTILABEL", &p );
    decodeFlag( &flags, MNT_CPROTECT, "CPROTECT", &p );
    decodeFlag( &flags, MNT_NOATIME, "NOATIME", &p );
    decodeFlag( &flags, MNT_SNAPSHOT, "SNAPSHOT", &p );
    decodeFlag( &flags, MNT_STRICTATIME, "STRICTATIME", &p );

    if (  flags  )
        p += sprintf( p, "unknown(0x%8.8x),", flags );

    if (  p > optbuf  )
        *--p = '\0';

    return optbuf;
} // decodeFsFlags
#else /* LINUX */
void
decodeFlag( unsigned long * flags, unsigned int fval, char * fname, char ** buf )
{
    if (  *flags & fval )
    {
        (*buf) += sprintf( *buf, "%s,", fname );
        *flags &= ~fval;
    }
} // decodeFlag

char *
decodeFsFlags( unsigned long flags )
{
static char optbuf[(FSOPTLEN*NUMFSOPT)+1];
    char * p = optbuf;

    memset( optbuf, 0, sizeof(optbuf) );

    decodeFlag( &flags, ST_RDONLY, "RDONLY", &p );
    decodeFlag( &flags, ST_NOSUID, "NOSUID", &p );
    decodeFlag( &flags, ST_NODEV, "NODEV", &p );
    decodeFlag( &flags, ST_NOEXEC, "NOEXEC", &p );
    decodeFlag( &flags, ST_SYNCHRONOUS, "SYNCHRONOUS", &p );
    decodeFlag( &flags, ST_MANDLOCK, "MANDLOCK", &p );
    decodeFlag( &flags, ST_WRITE, "WRITE", &p );
    decodeFlag( &flags, ST_APPEND, "APPEND", &p );
    decodeFlag( &flags, ST_IMMUTABLE, "IMMUTABLE", &p );
    decodeFlag( &flags, ST_NOATIME, "NOATIME", &p );
    decodeFlag( &flags, ST_NODIRATIME, "NODIRATIME", &p );
    decodeFlag( &flags, ST_RELATIME, "RELATIME", &p );

    if (  flags  )
        p += sprintf( p, "unknown(0x%8.8x),", flags );

    if (  p > optbuf  )
        *--p = '\0';

    return optbuf;
} // decodeFsFlags

char *
decodeFsType( long fstype )
{
static char fstbuf[33];

    switch (  fstype  )
    {
        case 0x42494e4d:
            strcpy( fstbuf, "BINFMT_MISC" );
            break;
        case 0x27E0EB:
            strcpy( fstbuf, "CGROUP" );
            break;
        case 0xFF534D42:
            strcpy( fstbuf, "CIFS" );
            break;
        case 0x62656570:
            strcpy( fstbuf, "CONFIGFS" );
            break;
        case 0x64626720:
            strcpy( fstbuf, "DEBUGFS" );
            break;
        case 0x1373:
            strcpy( fstbuf, "DEVFS" );
            break;
        case 0x137D:
            strcpy( fstbuf, "EXT" );
            break;
        case 0xEF51:
            strcpy( fstbuf, "OLD EXT2" );
            break;
        case 0xEF53:
            strcpy( fstbuf, "EXT2/3/4" );
            break;
        case 0x4244:
            strcpy( fstbuf, "HFS" );
            break;
        case 0xF995E849:
            strcpy( fstbuf, "HPFS" );
            break;
        case 0x958458f6:
            strcpy( fstbuf, "HUGETLBFS" );
            break;
        case 0x4d44:
            strcpy( fstbuf, "MSDOS" );
            break;
        case 0x19800202:
            strcpy( fstbuf, "MQUEUE" );
            break;
        case 0x6969:
            strcpy( fstbuf, "NFS" );
            break;
        case 0x6e667364:
            strcpy( fstbuf, "NFSD" );
            break;
        case 0x5346544E:
            strcpy( fstbuf, "NTFS" );
            break;
        case 0x9FA0:
            strcpy( fstbuf, "PROC" );
            break;
        case 0x6165676c:
            strcpy( fstbuf, "PSTORE" );
            break;
        case 0x52654973:
            strcpy( fstbuf, "REISERFS" );
            break;
        case 0x67596969:
            strcpy( fstbuf, "RPC_PIPEFS" );
            break;
        case 0x73636673:
            strcpy( fstbuf, "SECURITYFS" );
            break;
        case 0x517B:
            strcpy( fstbuf, "SMBFS" );
            break;
        case 0x62656572:
            strcpy( fstbuf, "SYSFS" );
            break;
        case 0x01021994:
            strcpy( fstbuf, "TMPFS" );
            break;
        case 0x15013346:
            strcpy( fstbuf, "UDF" );
            break;
        case 0x00011954:
            strcpy( fstbuf, "UFS" );
            break;
        case 0x786f4256:
            strcpy( fstbuf, "VBOXFS" );
            break;
        case 0x58465342:
            strcpy( fstbuf, "XFS" );
            break;
        default:
            sprintf( fstbuf, "0x%lx", fstype );
            break;
    }

    return fstbuf;
} // decodeFsType
#endif /* LINUX */

#if ! defined(LINUX)
void
displayStatFs( struct statfs * sfs, int dfmt )
{
    if (  dfmt == FMT_HUMAN )
    {
        printf("Filsystem type:   %s (0x%x/0x%x)\n", strUpper(sfs->f_fstypename), sfs->f_type, sfs->f_fssubtype);
        printf("Mount point:      %s\n", sfs->f_mntonname);
        printf("Mounted from:     %s\n", sfs->f_mntfromname);
        printf("Mounted by:       %lu (%s)\n", (unsigned long)sfs->f_owner, getUserName(sfs->f_owner));
        printf("Mount flags:      0x%8.8x (%s)\n", sfs->f_flags, decodeFsFlags(sfs->f_flags));
        printf("Block size:       %'u bytes\n", sfs->f_bsize);
        printf("Optimal I/O size: %'d bytes\n", sfs->f_iosize);
        printf("Total blocks:     %'llu\n", sfs->f_blocks);
        printf("Free blocks:      %'llu\n", sfs->f_bfree);
        printf("Available blocks: %'llu\n", sfs->f_bavail);
        printf("Total bytes:      %'llu\n", sfs->f_blocks*sfs->f_bsize);
        printf("Free bytes:       %'llu\n", sfs->f_bfree*sfs->f_bsize);
        printf("Available bytes:  %'llu\n", sfs->f_bavail*sfs->f_bsize);
        printf("Total inodes:     %'llu\n", sfs->f_files);
        printf("Free indodes:     %'llu\n", sfs->f_ffree);
        printf("Filesystem ID:    0x%8.8x%8.8x\n", sfs->f_fsid.val[0], sfs->f_fsid.val[1]);
    }
    else
    if (  dfmt == FMT_SHELL )
    {
        printf("f_bsize=%u ", sfs->f_bsize);
        printf("f_iosize=%d ", sfs->f_iosize);
        printf("f_blocks=%llu ", sfs->f_blocks);
        printf("f_bfree=%llu ", sfs->f_bfree);
        printf("f_bavail=%llu ", sfs->f_bavail);
        printf("f_files=%llu ", sfs->f_files);
        printf("f_ffree=%llu ", sfs->f_ffree);
        printf("f_fsid=0x%8.8x%8.8x ", sfs->f_fsid.val[0], sfs->f_fsid.val[1]);
        printf("f_owner=%lu ", (unsigned long)sfs->f_owner);
        printf("f_type=0x%x ", sfs->f_type);
        printf("f_flags='%s' ", decodeFsFlags(sfs->f_flags));
        printf("f_fssubtype=0x%x ", sfs->f_fssubtype);
        printf("f_fstypename='%s' ", strUpper(sfs->f_fstypename));
        printf("f_mntonname='%s' ", sfs->f_mntonname);
        printf("f_mntfromname='%s' ", sfs->f_mntfromname);
        printf("\n");
    }
    else
    {
        printf("%u ", sfs->f_bsize);
        printf("%d ", sfs->f_iosize);
        printf("%llu ", sfs->f_blocks);
        printf("%llu ", sfs->f_bfree);
        printf("%llu ", sfs->f_bavail);
        printf("%llu ", sfs->f_files);
        printf("%llu ", sfs->f_ffree);
        printf("0x%8.8x%8.8x ", sfs->f_fsid.val[0], sfs->f_fsid.val[1]);
        printf("%lu ", (unsigned long)sfs->f_owner);
        printf("0x%x ", sfs->f_type);
        printf("0x%8.8u ", sfs->f_flags);
        printf("0x%x ", sfs->f_fssubtype);
        printf("'%s' ", sfs->f_fstypename);
        printf("'%s' ", sfs->f_mntonname);
        printf("'%s' ", sfs->f_mntfromname);
        printf("\n");
    }
} // displayStatFs
#else /* LINUX */
void
displayStatFs( struct statfs * sfs, struct statvfs * svfs, int dfmt )
{
    if (  dfmt == FMT_HUMAN )
    {
        printf("Filsystem type:   %s\n", decodeFsType((unsigned long)sfs->f_type));
        printf("Block size:       %'u bytes\n", sfs->f_bsize);
        printf("Total blocks:     %'llu\n", sfs->f_blocks);
        printf("Free blocks:      %'llu\n", sfs->f_bfree);
        printf("Available blocks: %'llu\n", sfs->f_bavail);
        printf("Total bytes:      %'llu\n", sfs->f_blocks*sfs->f_bsize);
        printf("Free bytes:       %'llu\n", sfs->f_bfree*sfs->f_bsize);
        printf("Available bytes:  %'llu\n", sfs->f_bavail*sfs->f_bsize);
        printf("Total inodes:     %'llu\n", sfs->f_files);
        printf("Free indodes:     %'llu\n", sfs->f_ffree);
        printf("Max filename len: %ld\n", sfs->f_namelen);
        printf("Mount flags:      0x%8.8lx (%s)\n", svfs->f_flag, decodeFsFlags(svfs->f_flag));
    }
    else
    if (  dfmt == FMT_SHELL )
    {
        printf("f_type=%ld ", sfs->f_type);
        printf("f_bsize=%ld ", sfs->f_bsize);
        printf("f_blocks=%llu ", (long long unsigned int)sfs->f_blocks);
        printf("f_bfree=%llu ", (long long unsigned int)sfs->f_bfree);
        printf("f_bavail=%llu ", (long long unsigned int)sfs->f_bavail);
        printf("f_files=%llu ", (long long unsigned int)sfs->f_files);
        printf("f_ffree=%llu ", (long long unsigned int)sfs->f_ffree);
        printf("f_namelen=%ld ", (long)sfs->f_namelen);
        printf("f_flag='%s'", decodeFsFlags(svfs->f_flag));
        printf("\n");
    }
    else
    {
        printf("%ld ", sfs->f_type);
        printf("%ld ", sfs->f_bsize);
        printf("%llu ", (long long unsigned int)sfs->f_blocks);
        printf("%llu ", (long long unsigned int)sfs->f_bfree);
        printf("%llu ", (long long unsigned int)sfs->f_bavail);
        printf("%llu ", (long long unsigned int)sfs->f_files);
        printf("%llu ", (long long unsigned int)sfs->f_ffree);
        printf("%ld ", (long)sfs->f_namelen);
        printf("0x%8.8lx", svfs->f_flag);
        printf("\n");
    }
} // displayStatFs
#endif /* LINUX */

int
doStatFs( char * fpath, int dfmt )
{
    struct statfs sfs;
#if defined(LINUX)
    struct statvfs svfs;

    errno = 0;
    if (  statvfs( fpath, &svfs )  )
    {
        fprintf( stderr, "error: statvfs() failed %d/%s\n", errno, strerror( errno ) );
        return 1;
    }
#endif /* LINUX */
    errno = 0;
    if (  statfs( fpath, &sfs )  )
    {
        fprintf( stderr, "error: statfs() failed %d/%s\n", errno, strerror( errno ) );
        return 1;
    }

#if ! defined(LINUX)
    displayStatFs( &sfs, dfmt );
#else /* LINUX */
    displayStatFs( &sfs, &svfs, dfmt );
#endif /* LINUX */

    return 0;
} // doStatFs

#if ! defined(LINUX)
int
doGetFsStat( int dfmt )
{
    int numfs = 0, numfsr = 0, i;
    struct statfs * fslist = NULL;

    errno = 0;
    numfs = getfsstat( NULL, 0, 0 );
    if (  numfs < 0  )
    {
        fprintf( stderr, "error: getfsstat() failed %d/%s\n", errno, strerror( errno ) );
        return 1;
    }
    if (  numfs == 0  )
    {
        fprintf( stderr, "error: no mounted filesystems\n" );
        return 2;
    }

    fslist = (struct statfs *)calloc( numfs, sizeof(struct statfs) );
    if (  fslist == NULL  )
    {
        fprintf( stderr, "error: out of memory\n");
        return 3;
    }

    errno = 0;
    numfsr = getfsstat( fslist, numfs*sizeof(struct statfs), MNT_NOWAIT );
    if (  numfsr < 0  )
    {
        fprintf( stderr, "error: getfsstat() failed %d/%s\n", errno, strerror( errno ) );
        return 4;
    }
    if (  numfsr == 0  )
    {
        fprintf( stderr, "error: no mounted filesystems\n" );
        return 5;
    }
    if (  numfsr != numfs  )
        fprintf( stderr, "warning: filesystem count discrepancy (%d/%d)\n", numfs, numfsr );

    if (  dfmt == FMT_HUMAN )
        printf("%s\n", RPT_DIVIDER);
    for (i = 0 ; i < numfs; i++)
    {
        displayStatFs( fslist+i, dfmt );
        if (  dfmt == FMT_HUMAN )
            printf("%s\n", RPT_DIVIDER);
    }

    return 0;
} // doGetFsStat
#endif /* ! LINUX */

int
main( int argc, char *argv[] )
{
    int dformat = FMT_HUMAN;
    int argno = 1;
    int ret = 0;

    setlocale( LC_ALL, "" );

    if (  argc > 1  )
    {
        if (  strcmp( argv[argno], "-s" ) == 0  )
        {
            dformat = FMT_SHELL;
            argno += 1;
        }
        else
        if (  strcmp( argv[argno], "-r" ) == 0  )
        {
            dformat = FMT_RAW;
            argno += 1;
        }
        else
        if (  (strcmp( argv[argno], "-h" ) == 0) || (strcmp( argv[argno], "-help" ) == 0)  )
            usage();
    }
    if (  argc > (argno + 1)  )
        usage();
    else
    if (  argc > argno )
        ret = doStatFs( argv[argno], dformat );
    else
#if ! defined(LINUX)
        ret = doGetFsStat( dformat );
#else /* LINUX */
        usage();
#endif /* LINUX */

    return ret;
} // main
