/**********************************************************************
 *
 * IO Performance Test Utility (IOPS)
 *
 * Copyright (c) Chris Jenkins 2019, 2020
 *
 * Licensed under the Universal Permissive License v 1.0 as shown
 * at http://oss.oracle.com/licenses/upl
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <locale.h>
#include <signal.h>
#if defined(LINUX)
#define __USE_GNU
#include <fcntl.h>
#undef __USE_GNU
#else /* macOS */
#include <fcntl.h>
#endif /* macOS */
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#if defined(LINUX)
#include <sys/vfs.h>
#include <sys/statvfs.h>
#else /* ! LINUX */
#include <sys/param.h>
#include <sys/mount.h>
#endif /* ! LINUX */
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

/********************************************************************
 * Macros, constants, structures and types.
 */

#define  PROGNAME         "IOPS"
#define  VERSION          "2.3"

#define  ALLOW_RAW        1
#define  ALLOW_RAWWRITE   1
#define  ENV_RAWWRITE     "IOPSRawWrite"
#define  ENV_RAWVALUE     "YES"

#define  WAIT_US          10
#define  MSG_BUFF_SZ      256
#define  KB_MULT          1024L
#define  MB_MULT          (KB_MULT * KB_MULT)
#define  GB_MULT          (MB_MULT * KB_MULT)
#define  TB_MULT          (GB_MULT * KB_MULT)
#define  PB_MULT          (TB_MULT * KB_MULT)
#define  MIN_FSIZE        (1 * GB_MULT)
#define  MAX_FSIZE        (10 * GB_MULT)
#define  DFLT_FSIZE       MIN_FSIZE
#define  MIN_DUR          10
#define  MAX_DUR          3600
#define  DFLT_DUR         30
#define  MIN_RAMP         0
#define  MAX_RAMP         60
#define  DFLT_RAMP        10
#define  MIN_IOSZ         1
#define  MAX_IOSZ         (32 * MB_MULT)
#define  DFLT_IOSZ        (1 * MB_MULT)
#define  DFLT_GENIOSZ     (32 * MB_MULT)
#define  DFLT_FNAME       "iopsdata"
#define  MIN_THREADS      1
#define  MAX_THREADS      64
#define  DFLT_THREADS     1
#define  DFLT_VERBOSE     0
#define  MODE_UNKNOWN     0
#define  MODE_SEQUENTIAL  1
#define  MODE_RANDOM      2
#define  MODE_CREATE      3
#define  DFLT_MODE        MODE_UNKNOWN
#define  RET_INTR         127

typedef enum { DEFUNCT, RUNNING, RAMP, MEASURE, END, STOP } tstate_t;

struct s_context
{
    char * fname;
    char * tfname;
    void * genblk;
    void * ioblk;
    long   fsz;
    long   iosz;
    int    duration;
    int    ramp;
    int    noread;
    int    nowrite;
    long   geniosz;
    long   maxoffset;
    long   maxblock;
    long   blksz;
    long   optiosz;
    long   nreads;
    long   nwrites;
    long   preallocus;
    long   fsyncus;
    long   closeus;
    long   uscrstart;
    long   uscrstop;
    long   usrdstart;
    long   usrdstop;
    long   uswrstart;
    long   uswrstop;
    int    testmode;
    int    threads;
    int    usriosz;
    int    usrgeniosz;
    int    usriocnt;
    int    usrrdcnt;
    int    usrwrcnt;
    int    onefile;
    int    usrfile;
    int    cache;
    int    rdahead;
    int    nopreallocate;
    int    nodsync;
    int    nofsync;
    int    verbose;
    int    reportcpu;
    int    threadno;
    int    fd;
#if defined( ALLOW_RAW )
    int    raw;
    int    blk;
    int    rawwrite;
#endif /* ALLOW_RAW */
    int    retcode;
    volatile int crready;
    volatile int rdready;
    volatile int wrready;
    volatile int crstart;
    volatile int rdstart;
    volatile int wrstart;
    volatile tstate_t tstate;
    volatile int crfinished;
    volatile int rdfinished;
    volatile int wrfinished;
    long   crduration;
    long   rdduration;
    long   wrduration;
    char   msgbuff[MSG_BUFF_SZ];
    pthread_t tid;
};

typedef struct s_context context_t;

/********************************************************************
 * Global data
 */

volatile int signalReceived = 0;

struct timeval pstart, pend;
struct rusage rstart, rend;

context_t mctxt =
{
    DFLT_FNAME,
    NULL,
    NULL,
    NULL,
    DFLT_FSIZE,
    DFLT_IOSZ,
    DFLT_DUR,
    DFLT_RAMP,
    0,
    0,
    DFLT_GENIOSZ,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    0L,
    DFLT_MODE,
    DFLT_THREADS,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    DFLT_VERBOSE,
    0,
    0,
    -1,
#if defined( ALLOW_RAW )
    0,
    0,
    0,
#endif
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    DEFUNCT,
    0,
    0,
    0,
    0L,
    0L,
    0L,
    "",
    (pthread_t)NULL
};

context_t tctxt[MAX_THREADS];

/********************************************************************
 * Functions
 */

#if defined( ALLOW_RAW )

/*
 * Align offest to block size.
 */

off_t
alignOffset(
    off_t offset,
    long  blksz
           )
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
} // alignOffset

/*
 * Check if the block at a particular offset is readable.
 */

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
} // probeBlock

/*
 * Find the size of the raw/block device pointed to by 'fd'.
 */

long long
findRawSize(
    int fd,
    long blksz
           )
{
    void * ioblk = NULL;
    off_t poffset;
    off_t offset;
    off_t minsz = 0;
    off_t maxsz = PB_MULT;

    ioblk = (void *)valloc( blksz );
    if (  ioblk == NULL  )
        return -1;

    if (  ! probeBlock( fd, minsz, blksz, ioblk )  )
        offset = -1;
    else
    {
        offset = alignOffset( maxsz, blksz );
        if (  ! probeBlock( fd, offset, blksz, ioblk )  )
        {
            do {
                poffset = offset;;
                offset = alignOffset( ( (maxsz - minsz) / 2 ) + minsz, blksz );
                if (  probeBlock( fd, offset, blksz, ioblk )  )
                    minsz = offset;
                else
                    maxsz = offset;
            } while ( offset != poffset );
        }
    }

    free( ioblk );
    return ( offset + blksz );
} // findRawSize

#endif /* ALLOW_RAW */

/*
 * Signal handler.
 */

void
signalHandler(
    int signo
             )
{
    signalReceived = signo;
} // signalHandler

/*
 * Has a 'stop' interrupt been received?
 */

int
stopReceived(
    void
            )
{
    int ret;

    switch (  signalReceived  )
    {
        case SIGHUP:
        case SIGTERM:
        case SIGINT:
            ret = 1;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
} // stopReceived

/*
 * Setup signal handling
 */

int
handleSignals(
    void
             )
{
    struct sigaction sa;

    memset( (void *)&sa, 0, sizeof(struct sigaction) );
    sa.sa_handler = signalHandler;

    if (  sigaction( SIGINT, &sa, NULL )  )
        return 1;
    if (  sigaction( SIGHUP, &sa, NULL )  )
        return 1;
    if (  sigaction( SIGTERM, &sa, NULL )  )
        return 1;
    if (  sigaction( SIGUSR1, &sa, NULL )  )
        return 1;
    if (  sigaction( SIGUSR2, &sa, NULL )  )
        return 1;

    return 0;
} // handleSignals

/*
 * Display online help.
 */

void
usage(
      int full
     )
{
    printf("\nIOPS version %s\n\n", VERSION);

    printf("Usage:\n\n");

    printf("    iops { s[equential] | r[andom] } [-file <fpath>] [-fsize <fsz>] [-cpu]\n");
    printf("         [-iosz <tsz>] [-dur <tdur>] [-ramp <tramp>] [-noread | -nowrite]\n");
#if defined(ALLOW_RAW) && defined(ALLOW_RAWWRITE)
    printf("         [-geniosz <gsz>] [-threads <nthr>] [-verbose]\n");
    printf("         [-1file [<usrfpath> [-rawWrite]]]\n");
#else  /* ! ALLOW_RAW || ! ALLOW_RAWWRITE */
    printf("         [-geniosz <gsz>] [-threads <nthr>] [-verbose] [-1file [<usrfpath>]]\n");
#endif /* ! ALLOW_RAW || ! ALLOW_RAWWRITE */
#if defined(LINUX)
    printf("         [-nopreallocate] [-cache] [-nodysnc [-nofsync]]\n\n");
#else /* macOS */
    printf("         [-nopreallocate] [-rdahead] [-cache] [-nodysnc [-nofsync]]\n\n");
#endif /* macOS */

    printf("    iops c[reate] [-file <fpath>] [-fsize <fsz>] [-geniosz <gsz>]\n");
    printf("         [-nopreallocate] [-cpu]\n\n");

    printf("    iops h[elp]\n\n");

    if (  ! full  )
        exit( 100 );

    printf("Tests the I/O capability of storage devices, filesystems and OS.\n\n");

    printf("Parameters are:\n\n");

    printf("  s[equential]\n");
    printf("     Performs a sequential I/O test.\n\n");

    printf("  r[andom]\n");
    printf("     Performs a random I/O test.\n\n");

    printf("  c[reate]\n");
    printf("     Creates a file suitable for later use with the '-1file' option.\n\n");

    printf("  h[elp]\n");
    printf("     Display full help (this text).\n\n");

    printf("    -file <fpath>\n");
    printf("        Path of the file to use for testing. A separate file named 'fpath-nn'\n");
    printf("        will be created for each test thread, where 'nn' is the thread number,\n");
    printf("        unless the '-1file' option is used. The default value for <fpath> is\n");
    printf("        '%s'.\n\n", DFLT_FNAME);

    printf("        Files that will be created must not already exist. Any files created\n");
    printf("        will be removed automatically.\n\n");

    printf("    -fsize <fsz>\n");
    printf("        When creating test files, the size of each test file. When using an\n");
    printf("        existing file, the maximum offset within the file to be used when\n");
    printf("        testing. The value must be in the range %'ld GB to %'ld GB. When creating\n",
                    MIN_FSIZE/GB_MULT, MAX_FSIZE/GB_MULT );
    printf("        files the default is %'ld GB.\n\n", DFLT_FSIZE/GB_MULT );

    printf("        The size is specified in bytes but it can be specified in kilobytes\n");
    printf("        (1024 bytes), megabytes (1024*1024 bytes) or gigabytes (1024*1024*1024\n");
    printf("        bytes) by using a suffix of k, m or g on the value.\n\n");

#if ! defined(LINUX)
    printf("    -iosz <tsz>\n");
    printf("        The size of each test I/O request, specified in the same manner as\n");
    printf("        for '-fsize'. The value must be > 0 and <= %'ld MB. The default is\n",
                    MAX_IOSZ/MB_MULT );
    printf("        the optimal I/O size for the filesystem containing the test file(s)\n");
    printf("        or %'ld MB if that cannot be determined.\n\n",
                    DFLT_IOSZ/MB_MULT);

#if defined(ALLOW_RAW)
    printf("        When testing a block or raw device, the value must be a multiple of\n");
    printf("        the device's block size, as reported by 'stat', or the test will\n");
    printf("        fail.\n\n");
#endif /* ALLOW_RAW */

    printf("    -geniosz <gsz>\n");
    printf("        The size of each write request when creating the test file(s),\n");
    printf("        specified in the same manner as for '-fsize'. Must be > 0 and\n");
    printf("        <= <fsz>. The default is the closest multiple of the filesystem's\n");
    printf("        optimal I/O size to %'ld MB, or %'ld MB if that cannot be determined.\n\n",
                    DFLT_GENIOSZ/MB_MULT, DFLT_GENIOSZ/MB_MULT);

#else /* LINUX */

    printf("    -iosz <tsz>\n");
    printf("        The size of each test I/O request, specified in the same manner as\n");
    printf("        for '-fsize'. The value must be > 0 and <= %'ld MB. The default is\n",
                    MAX_IOSZ/MB_MULT );
    printf("        %'ld MB.\n\n", DFLT_IOSZ/MB_MULT);

#if defined(ALLOW_RAW)
    printf("        When testing a block or raw device, the value must be a multiple of\n");
    printf("        the device's block size, as reported by 'stat', or the test will\n");
    printf("        fail.\n\n");
#endif /* ALLOW_RAW */

    printf("    NOTE:\n");
    printf("        On Linux, caching is disabled by opening the file with the O_DIRECT\n");
    printf("        flag. This has the side effect of requiring all I/O to the file to be\n");
    printf("        filesystem block aligned. Hence on Linux, unless caching is enabled,\n");
    printf("        the value for '-iosz' must be a multiple of the filesystem's block size\n");
    printf("        the test will fail.\n\n");

    printf("    -geniosz <gsz>\n");
    printf("        The size of each write request when creating the test file(s),\n");
    printf("        specified in the same manner as for '-fsize'. Must be > 0 and\n");
    printf("        <= <fsz>. The default is %'ld MB.\n\n",
                    DFLT_GENIOSZ/MB_MULT);
#endif /* LINUX */

    printf("    -dur <tdur>\n");
    printf("        The duration of the measured part of the test in seconds. Must be\n");
    printf("        between %'d and %'d, the default is %'d\n\n",
                    MIN_DUR, MAX_DUR, DFLT_DUR);

    printf("    -ramp <tramp>\n");
    printf("        The ramp up/down time, before/after the measured part of the test,\n");
    printf("        in seconds. Must be between %'d and %'d, the default is %'d.\n\n",
                    MIN_RAMP, MAX_RAMP, DFLT_RAMP);

    printf("    NOTE:\n");
    printf("        The default measurement duration and ramp times have been chosen to\n");
    printf("        give good results across a wide range of storage systems.\n\n");

    printf("    -threads <nthr>\n");
    printf("        The number of concurrent threads to use for the test. The minimum\n");
    printf("        (and default) value is 1 and the maximum is %d. Threads are numbered\n",
                    MAX_THREADS);
    printf("        from 0. With fast devices (SSDs and similar) you will likely need to\n");
    printf("        use multiple threads in order to accurately measure the device's\n");
    printf("        maximum performance. For rotational devices (regular HDDs) using\n");
    printf("        multiple threads may be counter productive as it could result in\n");
    printf("        contention (though the results may still be interesting).\n\n");

    printf("    -cpu\n");
    printf("        Displays CPU usage information for the measurement part of each test.\n\n");

    printf("    -verbose\n");
    printf("        Displays additional, possibly interesting, information during\n");
    printf("        execution. Primarily per thread metrics.\n\n");

    printf("The following options are for special usage only. The objective of this tool\n");
    printf("is to measure the performance of storage hardware (as far as is possible\n");
    printf("given that a filesystem is interposed between the test program and the\n");
    printf("hardware). As a result certain OS features are used by default to try to\n");
    printf("achieve this. The setings below allow you to change aspects of the program's\n");
    printf("behaviour. This may be interesting but the results so achieved should be\n");
    printf("interpreted with caution.\n\n");

#if defined(ALLOW_RAW) && defined(ALLOW_RAWWRITE)
    printf("    -1file [<usrfpath> [-rawWrite]]\n");
#else  /* ! ALLOW_RAW || ! ALLOW_RAWWRITE */
    printf("    -1file [<usrfpath>]\n");
#endif /* ALLOW_RAW */
    printf("        Normally each test thread creates its own test file in order to avoid\n");
    printf("        any filesystem contention that might arise from multiple threads\n");
    printf("        performing I/O on the same file. When this option is specified, all\n");
    printf("        threads share the same test file. Each thread opens the file separately\n");
    printf("        but I/O operations are not synchronised between the threads.\n\n");

    printf("        Normally the test file is created automatically, but if the optional\n");
    printf("        <usrfpath> value is specified then that pre-existing file is used\n");
    printf("        instead.\n\n");

#if defined(ALLOW_RAW)
    printf("        <usrfpath> may refer to a block special or character special (raw) file\n");
#if ! defined(ALLOW_RAWWRITE)
    printf("        (device). In this case only read tests will be allowed.\n\n");
#else /* ALLOW_RAWWRITE */
    printf("        (device). In order to perform write tests on a block or raw device you\n");
    printf("        must both (a) set the environment variable named 'IOPSRawWrite' to the\n");
    printf("        value 'YES' and specify the '-rawWrite' option.\n\n");

    printf("    IMPORTANT WARNING:\n");
    printf("        Performing write tests on a block or raw device will irretrievably\n");
    printf("        corrupt any filesystem or other data on the device. YOU HAVE BEEN\n");
    printf("        WARNED!\n\n");
#endif /* ALLOW_RAWWRITE */
#endif /* ALLOW_RAW */

    printf("    NOTES:\n");
    printf("        - If a user file is specified it must be at least %'ld GB in size.\n\n",
                      MIN_FSIZE/GB_MULT);

    printf("        - If write testing is being performed (the default) then the contents\n");
    printf("          of the user file will be overwritten without warning!\n\n");

    printf("        - The user file will not be removed at the end of the test.\n\n");

    printf("        - Use of <usrfpath> is mutually exclusive with the '-file', '-geniosz'\n");
    printf("          and '-nopreallocate' options.\n\n");

    printf("        - The default for '-fsize' is the size of the user file. If you\n");
    printf("          explicitly specify a value for '-fsize' it must be <= the actual\n");
    printf("          file size. If the user file is larger than %'ld GB then the tests\n",
                      MAX_FSIZE/GB_MULT);
    printf("          will fail unless you use '-fsize' to limit the maximum offset\n");
    printf("          within the file.\n\n");

#if defined(ALLOW_RAW)
    printf("        - Testing a block or raw device will likely require you to execute this\n");
    printf("          utility as 'root'.\n\n");

    printf("        - You may not be able to test a block device if there is a filesystem\n");
    printf("          currently mounted on it. Even if the OS does not prohibit this you\n");
    printf("          are strongly advised not to do so.\n\n");

    printf("        - You may not be able to test a raw device if there is a filesystem\n");
    printf("          currently mounted on its corresponding block device. Even if the OS\n");
    printf("          does not prohibit this you are strongly advised not to do so.\n\n");
#endif /* ALLOW_RAW */

    printf("    -nopreallocate\n");
    printf("        Normally space for the test file(s) is pre-allocated (contiguously\n");
    printf("        if possible) using OS APIs. If this option is specified then the space\n");
    printf("        will not be pre-allocated.\n\n");

#if defined(ALLOW_RAW)
    printf("        Not allowed when testing a block or raw device.\n\n");
#endif /* ALLOW_RAW */

#if ! defined(LINUX)
    printf("    -rdahead\n");
    printf("        Normally OS read ahead is disabled for the test file(s). If this\n");
    printf("        option is specified then read ahead will not be explicitly disabled.\n\n");

#if defined(ALLOW_RAW)
    printf("        Not allowed when testing a block or raw device.\n\n");
#endif /* ALLOW_RAW */

#endif /* ! Linux */

    printf("    -cache\n");
    printf("        Normally OS filesystem caching is disabled for the test file(s). If\n");
    printf("        this option is specified then caching will not be explicitly disabled.\n\n");

#if defined(ALLOW_RAW)
    printf("        Not allowed when testing a block or raw device.\n\n");
#endif /* ALLOW_RAW */

#if defined(LINUX)
    printf("    IMPORTANT NOTE:\n");
    printf("        On Linux, OS caching is disabled by opening the file(s) with the\n");
    printf("        O_DIRECT flag. This has the side effect of requiring all I/O to the\n");
    printf("        file(s) to be filesystem block aligned. Hence on Linux, unless caching\n");
    printf("        is enabled, values for '-iosz' must be a multiple of the filesystem\n");
    printf("        block size or the test will fail.\n\n");

#endif /* LINUX */

    printf("    -nodsync\n");
    printf("        Normally the test file(s) are opened with the O_DSYNC flag. If this\n");
    printf("        option is specified then that flag will not be used.\n\n");

#if defined(ALLOW_RAW)
    printf("        Not allowed when testing a block or raw device.\n\n");
#endif /* ALLOW_RAW */

    printf("    -nofsync\n");
    printf("        If '-nodsync' is specified, then at the end of a write test each thread\n");
    printf("        will call the platform equivalent of fdatasync() on the file. If this\n");
    printf("        option is specified then that call is not made.\n\n");

#if defined(ALLOW_RAW)
    printf("        Not allowed when testing a block or raw device.\n\n");
#endif /* ALLOW_RAW */

    printf("    NOTES:\n");
    printf("        - The measured time for write tests includes the time for any 'close()'\n");
    printf("          or 'fdatasync()' operation that is part of the test.\n\n");

    printf("        - Due to an implementation quirk, the CPU time reported for write tests\n");
    printf("          does not include any 'fdatasync()' or 'close()' operations.\n\n");

    exit( 100 );
} // usage

/*
 * Sleep for a specified number of microseconds.
 */

void
usSleep(
        unsigned int us
       )
{
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };

    nanosleep( &ts, NULL );
} // usSleep

/*
 * Return the current time as microseconds since the epoch.
 */

long
getTimeAsUs(
            void
           )
{
    struct timeval tv;

    gettimeofday( &tv, NULL );
    return (long)tv.tv_usec + (1000000L * (long)tv.tv_sec);
} // getTimeAsUs

/*
 * Convert a string into an integer.
 */

int
intConvert(
           char * val,
           int  * ival
          )
{
    char * p;
    int iv, l;

    if (  ( val == NULL ) || ( ival == NULL )  )
        return 1;

    l = strlen( val );
    if ( ( l < 1 ) || ( l > 9 )  )
        return 1;

    iv = (int)strtol( val, &p, 10 );
    if (  *p  )
        return 1;

    *ival = iv;
    return 0;
} // intConvert

/*
 * Convert a string into a long integer.
 */

int
longConvert(
            char * val,
            long * lval
           )
{
    char * p;
    int l;
    long lv;

    if (  ( val == NULL ) || ( lval == NULL )  )
        return 1;

    l = strlen( val );
    if ( ( l < 1 ) || ( l > 18 )  )
        return 1;

    lv = strtol( val, &p, 10 );
    if (  *p  )
        return 1;

    *lval = lv;
    return 0;
} // longConvert

/*
 * Convert a string into a long integer. Allows use of k, m and g suffixes.
 */

int
valueConvert(
             char * val,
             long * lval
            )
{
    int l;
    char * p;
    long multiplier = 1, lv;

    if (  ( val == NULL ) || ( lval == NULL )  )
        return 1;

    l = strlen( val );
    if ( l < 1  )
        return 1;

    p = val + l - 1;
    switch (  *p  )
    {
        case 'K':
        case 'k':
            multiplier = KB_MULT;
            *p = '\0';
            l -= 1;
            break;

        case 'M':
        case 'm':
            multiplier = MB_MULT;
            *p = '\0';
            l -= 1;
            break;

        case 'G':
        case 'g':
            multiplier = GB_MULT;
            *p = '\0';
            l -= 1;
            break;
    }
    if ( l < 1  )
        return 1;

    lv = strtol( val, &p, 10 );
    if (  *p  )
        return 1;
    
    *lval = (lv * multiplier);
    return 0;
} // valueConvert

/*
 * Parse and validate the command line arguments.
 */

int
parseArgs(
          int    argc,
          int    argno,
          char * argv[],
          context_t * ctxt
         )
{
    int foundFile = 0, foundFsize = 0, foundIosz = 0, foundVerbose = 0, found1file = 0;
    int foundNoread = 0, foundNowrite = 0;
    int foundGeniosz = 0, foundNopreallocate = 0, foundRdahead = 0, foundUsrfile = 0;
    int foundCache = 0, foundNodsync = 0, foundNofsync = 0, foundThreads = 0;
    int foundCpu = 0, foundDur = 0, foundRamp = 0, foundRawwrite = 0;
    long long fsz;
    struct stat sbuf;
#if defined( ALLOW_RAW )
    int fd;
#endif /* ALLOW_RAW */

    while (  argno < argc  )
    {
        if (  strcmp( argv[argno], "-nopreallocate" ) == 0  )
        {
            if (  foundNopreallocate  )
            {
                fprintf( stderr, "\n*** Multiple '-nopreallocate' options not allowed\n" );
                return 1;
            }
            if (  foundUsrfile )
            {
                fprintf( stderr, "\n*** '-nopreallocate' is incompatible with a user specified filename\n" );
                return 1;
            }
            ctxt->nopreallocate = foundNopreallocate = 1;
        }
        else
#if ! defined(LINUX)
        if (  strcmp( argv[argno], "-rdahead" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundRdahead  )
            {
                fprintf( stderr, "\n*** Multiple '-rdahead' options not allowed\n" );
                return 1;
            }
            ctxt->rdahead = foundRdahead = 1;
        }
        else
#endif /* ! LINUX */
        if (  strcmp( argv[argno], "-1file" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  found1file  )
            {
                fprintf( stderr, "\n*** Multiple '-1file' options not allowed\n" );
                return 1;
            }
            ctxt->onefile = found1file = 1;
            if (  ((argno+1) < argc) && (argv[argno+1][0] != '-')  )
            {
                if (  foundFile )
                {
                    fprintf( stderr, "\n*** A user specified filename is incompatible with '-file'\n" );
                    return 1;
                }
                if (  foundGeniosz  )
                {
                    fprintf( stderr, "\n*** A user specified filename is incompatible with '-geniosz'\n" );
                    return 1;
                }
                ctxt->fname = argv[++argno];
                ctxt->usrfile = foundUsrfile = 1;
            }
        }
        else
#if defined(ALLOW_RAW) && defined(ALLOW_RAWWRITE)
        if (  strcmp( argv[argno], "-rawWrite" ) == 0  )
        {
            if (  foundRawwrite  )
            {
                fprintf( stderr, "\n*** Multiple '-rawWrite' options not allowed\n" );
                return 1;
            }
            foundRawwrite = 1;
            if (  ctxt->rawwrite < 0  )
                ctxt->rawwrite = 1;
            else
            {
                fprintf( stderr, "\n*** Writing to block and raw devices is not enabled\n" );
                return 1;
            }
        }
        else
#endif /* ALLOW_RAW && ALLOW_RAWWRITE */
        if (  strcmp( argv[argno], "-cpu" ) == 0  )
        {
            if (  foundCpu  )
            {
                fprintf( stderr, "\n*** Multiple '-cpu' options not allowed\n" );
                return 1;
            }
            ctxt->reportcpu = foundCpu = 1;
        }
        else
        if (  strcmp( argv[argno], "-noread" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundNoread  )
            {
                fprintf( stderr, "\n*** Multiple '-noread' options not allowed\n" );
                return 1;
            }
            if (  foundNowrite  )
            {
                fprintf( stderr, "\n*** '-noread' and '-nowrite' are mutually exclusive\n" );
                return 1;
            }
            ctxt->noread = foundNoread = 1;
        }
        else
        if (  strcmp( argv[argno], "-nowrite" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundNowrite  )
            {
                fprintf( stderr, "\n*** Multiple '-nowrite' options not allowed\n" );
                return 1;
            }
            if (  foundNoread  )
            {
                fprintf( stderr, "\n*** '-noread' and '-nowrite' are mutually exclusive\n" );
                return 1;
            }
            ctxt->nowrite = foundNowrite = 1;
        }
        else
        if (  strcmp( argv[argno], "-verbose" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundVerbose  )
            {
                fprintf( stderr, "\n*** Multiple '-verbose' options not allowed\n" );
                return 1;
            }
            ctxt->verbose = foundVerbose = 1;
        }
        else
        if (  strcmp( argv[argno], "-cache" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundCache  )
            {
                fprintf( stderr, "\n*** Multiple '-cache' options not allowed\n" );
                return 1;
            }
            ctxt->cache = foundCache = 1;
        }
        else
        if (  strcmp( argv[argno], "-nodsync" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundNodsync  )
            {
                fprintf( stderr, "\n*** Multiple '-nodsync' options not allowed\n" );
                return 1;
            }
            ctxt->nodsync = foundNodsync = 1;
        }
        else
        if (  strcmp( argv[argno], "-nofsync" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundNofsync  )
            {
                fprintf( stderr, "\n*** Multiple '-nofsync' options not allowed\n" );
                return 1;
            }
            ctxt->nofsync = foundNofsync = 1;
        }
        else
        if (  strcmp( argv[argno], "-file" ) == 0  )
        {
            if (  foundFile  )
            {
                fprintf( stderr, "\n*** Multiple '-file' options not allowed\n" );
                return 1;
            }
            if (  foundUsrfile )
            {
                fprintf( stderr, "\n*** '-file' is incompatible with a user specified filename\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-file'\n" );
                return 1;
            }
            ctxt->fname = argv[argno];
            foundFile = 1;
        }
        else
        if (  strcmp( argv[argno], "-fsize" ) == 0  )
        {
            if (  foundFsize  )
            {
                fprintf( stderr, "\n*** Multiple '-fsize' options not allowed\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-fsize'\n" );
                return 1;
            }
            if (  valueConvert( argv[argno], &ctxt->fsz )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-fsize'\n" );
                return 1;
            }
            foundFsize = 1;
        }
        else
        if (  strcmp( argv[argno], "-iosz" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundIosz  )
            {
                fprintf( stderr, "\n*** Multiple '-iosz' options not allowed\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-iosz'\n" );
                return 1;
            }
            if (  valueConvert( argv[argno], &ctxt->iosz )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-iosz'\n" );
                return 1;
            }
            if (  (ctxt->iosz < MIN_IOSZ) || (ctxt->iosz > MAX_IOSZ)  )
            {
                fprintf( stderr, "\n*** Invalid value for '-iosz'\n" );
                return 1;
            }
            ctxt->usriosz = foundIosz = 1;
        }
        else
        if (  strcmp( argv[argno], "-threads" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundThreads )
            {
                fprintf( stderr, "\n*** Multiple '-threads' options not allowed\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-threads'\n" );
                return 1;
            }
            if (  intConvert( argv[argno], &ctxt->threads )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-threads'\n" );
                return 1;
            }
            foundThreads = 1;
        }
        else
        if (  strcmp( argv[argno], "-dur" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundDur )
            {
                fprintf( stderr, "\n*** Multiple '-dur' options not allowed\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-dur'\n" );
                return 1;
            }
            if (  intConvert( argv[argno], &ctxt->duration )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-dur'\n" );
                return 1;
            }
            if (  (ctxt->duration < MIN_DUR) || (ctxt->duration > MAX_DUR)  )
            {
                fprintf( stderr, "\n*** Invalid value for '-dur'\n" );
                return 1;
            }
            foundDur = 1;
        }
        else
        if (  strcmp( argv[argno], "-ramp" ) == 0  )
        {
            if (  ctxt->testmode == MODE_CREATE  )
            {
                fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
                return 1;
            }
            if (  foundRamp )
            {
                fprintf( stderr, "\n*** Multiple '-ramp' options not allowed\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-ramp'\n" );
                return 1;
            }
            if (  intConvert( argv[argno], &ctxt->ramp )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-ramp'\n" );
                return 1;
            }
            if (  (ctxt->ramp < MIN_RAMP) || (ctxt->ramp > MAX_RAMP)  )
            {
                fprintf( stderr, "\n*** Invalid value for '-ramp'\n" );
                return 1;
            }
            foundRamp = 1;
        }
        else
        if (  strcmp( argv[argno], "-geniosz" ) == 0  )
        {
            if (  foundGeniosz  )
            {
                fprintf( stderr, "\n*** Multiple '-geniosz' options not allowed\n" );
                return 1;
            }
            if (  foundUsrfile )
            {
                fprintf( stderr, "\n*** '-geniosize' is incompatible with a user specified filename\n" );
                return 1;
            }
            if (  ++argno >= argc  )
            {
                fprintf( stderr, "\n*** Missing value for '-geniosz'\n" );
                return 1;
            }
            if (  valueConvert( argv[argno], &ctxt->geniosz )  )
            {
                fprintf( stderr, "\n*** Invalid value for '-geniosz'\n" );
                return 1;
            }
            ctxt->usrgeniosz = foundGeniosz = 1;
        }
        else
        {
            fprintf( stderr, "\n*** Invalid argument '%s'\n", argv[argno] );
            return 1;
        }
        argno += 1;
    }

    ctxt->tfname = ctxt->fname;

    if (  foundFsize && ( (ctxt->fsz < MIN_FSIZE) || (ctxt->fsz > MAX_FSIZE) )  )
    {
        fprintf( stderr, "\n*** Invalid value for '-fsize'\n" );
        return 1;
    }

    if (  ctxt->usrfile  )
    {
#if defined( ALLOW_RAW )
        if (  ctxt->rawwrite < 0  )
            ctxt->rawwrite = 0;
        errno = 0;
        fd = open( ctxt->fname, O_RDONLY );
        if (  fd < 0  )
        {
            fprintf( stderr, "\n*** Unable to open '%s'  - %d (%s)\n", ctxt->fname,
                     errno, strerror(errno) );
            return 1;
        }
        errno = 0;
        if (  fstat( fd, &sbuf )  )
        {
            fprintf( stderr, "\n*** Unable to stat '%s'   - %d (%s)\n", ctxt->fname,
                     errno, strerror(errno) );
            close( fd );
            return 1;
        }
        if (  ! ( sbuf.st_mode & S_IFREG ) &&
              ! ( sbuf.st_mode & S_IFCHR ) &&
              ! ( sbuf.st_mode & S_IFBLK )  )
        {
            fprintf( stderr, "\n*** '%s' is not a regular, block or raw file\n", ctxt->fname );
            close( fd );
            return 1;
        }
        if (  ( sbuf.st_mode & S_IFCHR ) || ( sbuf.st_mode & S_IFBLK )  )
        {
            ctxt->raw = 1;
            if (  sbuf.st_mode & S_IFBLK  )
                ctxt->blk = 1;
            if (  foundRdahead  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, '-rdahead' not allowed\n", ctxt->fname );
                close( fd );
                return 1;
            }
            if (  foundCache  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, '-cache' not allowed\n", ctxt->fname );
                close( fd );
                return 1;
            }
            if (  foundNopreallocate  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, '-nopreallocate' not allowed\n", ctxt->fname );
                close( fd );
                return 1;
            }
            if (  foundNodsync  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, '-nodsync' not allowed\n", ctxt->fname );
                close( fd );
                return 1;
            }
            if (  foundNofsync  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, '-nofsync' not allowed\n", ctxt->fname );
                close( fd );
                return 1;
            }
            if (  ctxt->noread && ! ctxt->rawwrite  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, raw writes are not enabled and '-noread' specified\n",
                         ctxt->fname );
                close( fd );
                return 1;
            }
            if (  ctxt->noread  )
            {
#if ! defined(ALLOW_RAWWRITE)
                fprintf( stderr, "\n*** '%s' is a block or raw file, raw writes are not allowed and '-noread' specified\n",
                         ctxt->fname );
                close( fd );
                return 1;
#else  /* ALLOW_RAWWRITE */
                if (  ! ctxt->rawwrite  )
                {
                    fprintf( stderr, "\n*** '%s' is a block or raw file, raw writes are not enabled and '-noread' specified\n",
                             ctxt->fname );
                    close( fd );
                    return 1;
                }
#endif /* ALLOW_RAWWRITE */
            }
#if ! defined(ALLOW_RAWWRITE)
            ctxt->nowrite = foundNowrite = 1;
#else  /* ALLOW_RAWWRITE */
            if (  ! ctxt->nowrite  )
            {
                if (  ! ctxt->rawwrite  )
                    ctxt->nowrite = foundNowrite = 1;
            }
#endif /* ALLOW_RAWWRITE */

            ctxt->blksz = sbuf.st_blksize;
            if (  foundIosz && ( ( ctxt->iosz % ctxt->blksz ) != 0 )  )
            {
                fprintf( stderr, "\n*** '%s' is a block or raw file, value for '-iosz' must be a multiple of %'ld\n",
                         ctxt->fname, ctxt->blksz );
                close( fd );
                return 1;
            }

            fsz = findRawSize( fd, ctxt->blksz );
            if (  fsz <= 0  )
            {
                fprintf( stderr, "*** Unable to determine size for '%s'\n", ctxt->fname );
                close( fd );
                return 1;
            }

            close( fd );
        }
        else
            fsz = sbuf.st_size;
#else /* !ALLOW_RAW */
        errno = 0;
        if (  stat( ctxt->fname, &sbuf )  )
        {
            fprintf( stderr, "\n*** Unable to stat '%s'   - %d (%s)\n", ctxt->fname,
                     errno, strerror(errno) );
            return 1;
        }
        if (  ! ( sbuf.st_mode & S_IFREG )  )
        {
            fprintf( stderr, "\n*** '%s' is not a regular file\n", ctxt->fname );
            return 1;
        }
        fsz = sbuf.st_size;
#endif /* ALLOW_RAW */
        if (  foundFsize  )
        {
            if (  ctxt->fsz > fsz  )
            {
                fprintf( stderr, "\n*** Value specified for '-fsize' (%'ld) is greater than size of '%s'\n",
                         ctxt->fsz, ctxt->fname );
                return 1;
            }
        }
        else
        {
            if (  fsz < MIN_FSIZE  )
            {
                fprintf( stderr, "\n*** File size (%'lld) less than %'ld\n", fsz, (long)MIN_FSIZE );
                return 1;
            }
            if (  fsz > MAX_FSIZE  )
            {
                fprintf( stderr, "\n*** File size (%'lld) greater than %'ld\n", fsz, (long)MAX_FSIZE );
                return 1;
            }
            ctxt->fsz = (long)fsz;
        }
    } // usrfile

    if (  (ctxt->geniosz < 1) || (ctxt->geniosz > ctxt->fsz)  )
    {
        fprintf( stderr, "\n*** Invalid value for '-geniosz'\n" );
        return 1;
    }

    if (  foundFile && found1file && ! foundUsrfile  )
    {
        if (  stat( ctxt->fname, &sbuf ) == 0  )
        {
            fprintf( stderr, "\n*** File '%s' already exists\n", ctxt->fname );
            return 1;
        }
    }

    if (  ctxt->testmode != MODE_CREATE  )
    {
        if (  ctxt->nofsync && ! ctxt->nodsync  )
        {
            fprintf( stderr, "\n*** '-nofsync' can only be specified with '-nodsync'\n" );
            return 1;
        }
    
        if (  (ctxt->iosz < 1) || (ctxt->iosz > ctxt->fsz)  )
        {
            fprintf( stderr, "\n*** Invalid value for '-iosz'\n" );
            return 1;
        }
    
        if (  ( ctxt->threads < MIN_THREADS ) || ( ctxt->threads > MAX_THREADS )  )
        {
            fprintf( stderr, "\n*** Invalid value for '-threads'\n" );
            return 1;
        }
    }
    
    return 0;
} // parseArgs

/*
 * Report CPU usage information.
 */

void
reportTimes(
            void
           )
{
    long cpu_time = 0;
    int  cpu_ustime = 0;
    long cpu_user = 0;
    int  cpu_ususer = 0;
    long cpu_sys = 0;
    int  cpu_ussys = 0;
    long pcpu_seconds = 0;
    int  pcpu_useconds = 0;
    long numcpus = 1;
    long totcpuus = 0;
    long usedcpuus = 0;
    double proccpu = 0.0;
    double syscpu = 0.0;

    cpu_user = rend.ru_utime.tv_sec - rstart.ru_utime.tv_sec;
    cpu_ususer = rend.ru_utime.tv_usec - rstart.ru_utime.tv_usec;
    if (  cpu_ususer < 0  )
    {
        cpu_user -= 1;
        cpu_ususer += 1000000;
    }

    cpu_sys = rend.ru_stime.tv_sec - rstart.ru_stime.tv_sec;
    cpu_ussys = rend.ru_stime.tv_usec - rstart.ru_stime.tv_usec;
    if (  cpu_ussys < 0  )
    {
        cpu_sys -= 1;
        cpu_ussys += 1000000;
    }

    cpu_time = cpu_user + cpu_sys;
    cpu_ustime = cpu_ususer + cpu_ussys;
    if (  cpu_ustime >= 1000000  )
    {
        cpu_time += 1;
        cpu_ustime %= 1000000;
    }

    pcpu_seconds = pend.tv_sec - pstart.tv_sec;
    pcpu_useconds = pend.tv_usec - pstart.tv_usec;
    if (  pcpu_useconds < 0  )
    {
        pcpu_seconds -= 1;
        pcpu_useconds += 1000000;
    }

    numcpus = sysconf( _SC_NPROCESSORS_ONLN );
    totcpuus = numcpus * ( ( pcpu_seconds * 1000000 ) + pcpu_useconds );
    usedcpuus = ( cpu_time * 1000000 ) + cpu_ustime;
    proccpu = ( 100.0 * (double)usedcpuus ) / (double)( ( pcpu_seconds * 1000000 ) + pcpu_useconds );
    syscpu = ( 100.0 * (double)usedcpuus ) / (double)totcpuus;

    printf( "Elapsed time = %'ld.%3.3d seconds\n", pcpu_seconds, pcpu_useconds/1000 );
    printf( "User CPU time = %'ld.%3.3d seconds\n", cpu_user, cpu_ususer/1000 );
    printf( "System CPU time = %'ld.%3.3d seconds\n", cpu_sys, cpu_ussys/1000 );
    printf( "Total CPU time = %'ld.%3.3d seconds\n", cpu_time, cpu_ustime/1000 );
    printf( "Process CPU usage = %.3f%%\n", proccpu );
    printf( "System CPU usage = %.3f%%\n", syscpu );
} // reportTimes

/*
 * Open the test file, optionally creating it.
 */

int
openFile(
          context_t * ctxt,
          int create
         )
{
    int flags, ret;
    long startus, stopus;
    struct stat sbuf;
    struct statfs fsbuf;

    if (  create && ! stat( ctxt->tfname, &sbuf )  )
    {
        if (  ctxt->threads > 1  )
            fprintf( stderr, "*** Thread %d: file '%s' already exists\n", 
                     ctxt->threadno, ctxt->tfname );
        else
            fprintf( stderr, "*** File '%s' already exists\n", ctxt->tfname );
        return 1;
    }

#if defined( ALLOW_RAW )
    if (  ctxt->raw  )
    {
#if defined(ALLOW_RAWWRITE)
        if (  ctxt->rawwrite && ! ctxt->nowrite  )
            flags = O_RDWR;
        else
#endif /* ALLOW_RAWWRITE */
            flags = O_RDONLY;
    }
    else
    {
        flags = O_RDWR;
        if (  create  )
            flags |= O_CREAT|O_EXCL;
        if (  ! ctxt->nodsync  )
            flags |= O_DSYNC;
#if defined(LINUX)
        if (  ! ctxt->cache  )
            flags |= O_DIRECT;
#endif /* Linux */
    }
#else /* ! ALLOW_RAW */
    flags = O_RDWR;
    if (  create  )
        flags |= O_CREAT|O_EXCL;
    if (  ! ctxt->nodsync  )
        flags |= O_DSYNC;
#if defined(LINUX)
    if (  ! ctxt->cache  )
        flags |= O_DIRECT;
#endif /* Linux */
#endif /* ! ALLOW_RAW */
    errno = 0;
    ctxt->fd = open( ctxt->tfname, flags, 0600 );
    if (  ctxt->fd < 0  )
    {
        if (  ctxt->threads > 1  )
            fprintf( stderr, "*** Thread %d: unable to %s file '%s' - %s\n",
                     ctxt->threadno, create?"create":"open", ctxt->tfname, strerror(errno) );
        else
            fprintf( stderr, "*** Unable to %s file '%s' - %s\n",
                     create?"create":"open", ctxt->tfname, strerror(errno) );
        return 1;
    }

#if defined( ALLOW_RAW )
    if (  ! ctxt->raw  )
    {
#endif /* ALLOW_RAW */
        if (  fstatfs( ctxt->fd, &fsbuf ) == 0  )
        {
            if ( fsbuf.f_bsize > 0 )
                ctxt->blksz = fsbuf.f_bsize;
#if ! defined(LINUX)
            if ( fsbuf.f_iosize > 0 )
                ctxt->optiosz = fsbuf.f_iosize;
#endif /* ! LINUX */
        }
#if defined( ALLOW_RAW )
    }
#endif /* ALLOW_RAW */

#if ! defined(LINUX)
    if (  ctxt->verbose && ! ctxt->usriosz && ! ctxt->optiosz  )
    {
        if (  ctxt->threads > 1  )
            printf("Thread %d: unable to determine optimal I/O size so using %'ld bytes\n",
                    ctxt->threadno, DFLT_IOSZ );
        else
            printf("Unable to determine optimal I/O size so using %'ld bytes\n",
                    DFLT_IOSZ );
    }
#endif /* ! LINUX */
    if (  ! ctxt->optiosz  )
        ctxt->optiosz = DFLT_IOSZ;

#if ! defined(LINUX)
#if defined( ALLOW_RAW )
    if ( ! ctxt->raw  )
    {
#endif /* ALLOW_RAW */
        if (  ! ctxt->rdahead  )
        {
            if (  fcntl( ctxt->fd, F_RDAHEAD, 0 )  )
            {
                if (  ctxt->threads > 1  )
                    fprintf( stderr, "*** Thread %d: unable to disable read-ahead for '%s'\n",
                             ctxt->threadno, ctxt->tfname );
                else
                    fprintf( stderr, "*** unable to disable read-ahead for '%s'\n",
                             ctxt->tfname );
                return 1;
            }
        }
    
        if (  ! ctxt->cache  )
        {
            if (  fcntl( ctxt->fd, F_NOCACHE, 1 )  )
            {
                if (  ctxt->threads > 1  )
                    fprintf( stderr, "*** Thread %d: unable to disable caching for '%s'\n",
                             ctxt->threadno, ctxt->tfname );
                else
                    fprintf( stderr, "*** : Unable to disable caching for '%s'\n",
                             ctxt->tfname );
                return 1;
            }
        }
#if defined( ALLOW_RAW )
    }
#endif /* ALLOW_RAW */
#endif /* macOS */

    if ( ! ctxt->usrfile && ! ctxt->nopreallocate  )
    {
#if defined(LINUX)
        startus = getTimeAsUs();
        ret = posix_fallocate( ctxt->fd, (off_t)0, (off_t)ctxt->fsz );
        stopus = getTimeAsUs();
        ctxt->preallocus = stopus - startus;
        if (  ctxt->threads > 1  )
        {
            if (  ret == 0  )
            {
                if (  ctxt->verbose  )
                    printf("Thread %d: preallocated %'ld bytes in %'ld µs\n", ctxt->threadno, ctxt->fsz, ctxt->preallocus );
            }
            else
                printf("Thread %d: preallocation failed or is not supported\n", ctxt->threadno );
        }
#else /* macOS */
        fstore_t prealloc;

        // Let's try and pre-allocate the space, just for fun
        prealloc.fst_flags = F_ALLOCATECONTIG|F_ALLOCATEALL;
        prealloc.fst_posmode = F_PEOFPOSMODE;
        prealloc.fst_offset = (off_t)0;
        prealloc.fst_length = (off_t)ctxt->fsz;
        prealloc.fst_bytesalloc = 0;
        startus = getTimeAsUs();
        ret = fcntl( ctxt->fd, F_PREALLOCATE, &prealloc );
        stopus = getTimeAsUs();
        ctxt->preallocus = stopus - startus;
        if (  ret == 0  )
        {
            if (  ctxt->verbose  )
            {
                if (  ctxt->threads > 1  )
                    printf("Thread %d: preallocated %'ld contiguous bytes in %'ld µs\n",
                            ctxt->threadno, (long)prealloc.fst_bytesalloc, ctxt->preallocus);
            }
        }
        else
        {
            prealloc.fst_flags = F_ALLOCATEALL;
            prealloc.fst_bytesalloc = 0;
            startus = getTimeAsUs();
            ret = fcntl( ctxt->fd, F_PREALLOCATE, &prealloc );
            stopus = getTimeAsUs();
            ctxt->preallocus = stopus - startus;
            if (  ret == 0  )
            {
                if (  ctxt->verbose  )
                {
                    if (  ctxt->threads > 1  )
                        printf("Thread %d: preallocated %'ld bytes in %'ld µs\n",
                                ctxt->threadno, (long)prealloc.fst_bytesalloc, ctxt->preallocus);
                }
            }
            else
            {
                if (  ctxt->threads > 1  )
                    printf("Thread %d: preallocation failed or is not supported\n",
                           ctxt->threadno );
                else
                    printf("Preallocation failed or is not supported\n");
            }
        }
#endif /* macOS */
    } // pre-allocate

    return 0;
} // openFile


/*
 * Setup a bunch of stuff ready for the specific test.
 */

int
initTests(
          context_t * ctxt
         )
{
    long nblocks, divvy, rem1, rem2;
    int ret = 0;

    if (  (ctxt->usrfile) || (ctxt->onefile && (ctxt->threadno > 0))  )
        ret = openFile( ctxt, 0 );
    else
        ret = openFile( ctxt, 1 );
    if (  ret  )
        return 1;

    if (  ctxt->optiosz && ! ctxt->usriosz  )
        ctxt->iosz = ctxt->optiosz;

    if (  ctxt->optiosz && ! ctxt->usrgeniosz  )
    {
        divvy = DFLT_GENIOSZ / ctxt->optiosz;
        rem1 = DFLT_GENIOSZ % ctxt->optiosz;
        rem2 = (DFLT_GENIOSZ + ctxt->optiosz) % ctxt->optiosz;
        ctxt->geniosz = (divvy * ctxt->optiosz);
        if (  rem2 > rem1  )
            ctxt->geniosz += ctxt->optiosz;
    }

    ctxt->maxoffset = ((ctxt->fsz / ctxt->iosz) + 1) * ctxt->iosz;
    nblocks = ( ctxt->maxoffset / ctxt->iosz );
    if (  nblocks > (long)RAND_MAX  )
        nblocks = (long)RAND_MAX + 1L;
    ctxt->maxblock = nblocks - 1;

    ctxt->genblk = valloc( ctxt->geniosz );
    if (  ctxt->genblk == NULL  )
    {
        if (  ctxt->threads > 1  )
            fprintf( stderr, "*** Thread %d: unable to valloc %'ld bytes\n", 
                     ctxt->threadno, ctxt->geniosz );
        else
            fprintf( stderr, "*** Unable to valloc %'ld bytes\n", 
                     ctxt->geniosz );
        return 1;
    }

    ctxt->ioblk = valloc( ctxt->iosz );
    if (  ctxt->ioblk == NULL  )
    {
        if (  ctxt->threads > 1  )
            fprintf( stderr, "*** Thread %d: unable to valloc %'ld bytes\n", 
                     ctxt->threadno, ctxt->iosz );
        else
            fprintf( stderr, "*** Unable to valloc %'ld bytes\n", 
                     ctxt->iosz );
        return 1;
    }

    return 0;
} // initTests

/*
 * Setup the thread contexts for the test.
 */

int
initContexts(
             context_t * mainctxt,
             context_t   threadcontexts[],
             int         numcontexts
            )
{
    int i, l, ret = 0;
#if defined( ALLOW_RAW )
    int fd;
    struct stat sbuf;
#endif /* ALLOW_RAW */

#if defined( ALLOW_RAW )
    if (  mainctxt->raw  )
    {
        errno = 0;
        fd = open( mainctxt->fname, O_RDONLY, 0 );
        if (  fd < 0  )
        {
            fprintf( stderr, "*** Unable to open '%s' - %d (%s)\n",
                     mainctxt->fname, errno, strerror(errno) );
            return 1;
        }
        if (  fstat( fd, &sbuf )  )
        {
            fprintf( stderr, "*** Unable to stat '%s' - %d (%s)\n",
                     mainctxt->fname, errno, strerror(errno) );
            close( fd );
            return 1;
        }
        mainctxt->blksz = sbuf.st_blksize;
        mainctxt->optiosz = sbuf.st_blksize;
        mainctxt->fsz = findRawSize( fd, mainctxt->blksz );
        if (  mainctxt->fsz <= 0  )
        {
            fprintf( stderr, "*** Unable to determine size for '%s'\n",
                     mainctxt->fname );
            close( fd );
            return 1;
        }
        close( fd );
    }
#endif /* ALLOW_RAW */

    if ( mainctxt->verbose && (mainctxt->threads > 1) )
        printf("\n");
    l = strlen( mainctxt->fname ) + 8;
    for ( i = 0; i < numcontexts; i++ )
    {
        memcpy( (void *)&threadcontexts[i], (void *)mainctxt, sizeof(context_t) );
        threadcontexts[i].threadno = i;
        threadcontexts[i].tfname = (char *)calloc( l, sizeof(char) );
        if (  threadcontexts[i].tfname == NULL  )
        {
            if (  mainctxt->threads > 1  )
                fprintf( stderr, "*** Thread %d: unable to malloc %d bytes\n",
                         mainctxt->threadno, l );
            else
                fprintf( stderr, "*** Unable to malloc %d bytes\n", l );
            ret = 1;
            break;
        }
        threadcontexts[i].crfinished = 0;
        threadcontexts[i].rdfinished = 0;
        threadcontexts[i].wrfinished = 0;
        if (  mainctxt->usrfile  )
            strcpy( threadcontexts[i].tfname, mainctxt->fname );
        else
        if (  mainctxt->onefile  )
            sprintf(  threadcontexts[i].tfname, "%s-%2.2d", mainctxt->fname, 0  );
        else
            sprintf(  threadcontexts[i].tfname, "%s-%2.2d", mainctxt->fname, i  );
        if (  initTests( &threadcontexts[i] )  )
        {
            ret = 1;
            break;
        }
    }

    mainctxt->blksz = threadcontexts[0].blksz;
    mainctxt->optiosz = threadcontexts[0].optiosz;
    mainctxt->iosz = threadcontexts[0].iosz;
    mainctxt->geniosz = threadcontexts[0].geniosz;

    if (  ! mainctxt->usrfile  )
    {
        if (  mainctxt->onefile  )
        {
            if (  threadcontexts[0].tfname != NULL  )
                unlink( threadcontexts[0].tfname );
        }
        else
        for ( i = 0; i < numcontexts; i++ )
        {
            if (  threadcontexts[i].tfname != NULL  )
                unlink( threadcontexts[i].tfname );
        }
    }

    return ret;
} // initContexts

/*
 * cleanup the thread contexts.
 */

void
cleanupContexts(
                context_t  threadcontexts[],
                int        numcontexts
               )
{
    int i, verbose, onefile;

    verbose = threadcontexts[i].verbose;
    onefile = threadcontexts[i].onefile;
    for ( i = 0; i < numcontexts; i++ )
    {
        if (  threadcontexts[i].fd >= 0  )
        {
            close( threadcontexts[i].fd );
            threadcontexts[i].fd = -1;
        }
        if (  threadcontexts[i].genblk != NULL  )
        {
            free( (void *)threadcontexts[i].genblk );
            threadcontexts[i].genblk = NULL;
        }
        if (  threadcontexts[i].ioblk != NULL  )
        {
            free( (void *)threadcontexts[i].ioblk );
            threadcontexts[i].ioblk = NULL;
        }
    }
} // cleanupContexts

/*
 * Generate the test file for a context.
 */

int
generateFile(
             context_t * ctxt,
             int doclose
            )
{
    long numblks = 0L, remainder = 0L, blkno;
    long startus, stopus;
    ssize_t nbytes;

    numblks = ctxt->fsz / ctxt->geniosz;
    remainder = ctxt->fsz % ctxt->geniosz;

    if (  stopReceived()  )
        return RET_INTR;

    ctxt->uscrstop = ctxt->uscrstart = getTimeAsUs();
    for ( blkno = 0L; blkno < numblks; blkno++ )
    {
        nbytes = write( ctxt->fd, ctxt->genblk, (size_t)ctxt->geniosz );
        if (  nbytes != ctxt->geniosz  )
        {
            sprintf( ctxt->msgbuff, "%srite failed for block %'ld", 
                     (ctxt->threads>1)?"w":"W", blkno );
            return 1;
        }
        if (  stopReceived()  )
            return RET_INTR;
    }
    if (  remainder > 0  )
    {
        nbytes = write( ctxt->fd, ctxt->genblk, (size_t)remainder );
        if (  nbytes != remainder  )
        {
            sprintf( ctxt->msgbuff, "%srite failed for block %'ld", 
                     (ctxt->threads>1)?"w":"W", blkno );
            return 1;
        }
    }

    if (  stopReceived()  )
        return RET_INTR;

    if (  ctxt->nodsync && ! ctxt->nofsync  )
    {
        startus = getTimeAsUs();
        errno = 0;
#if defined(LINUX)
        if (  fdatasync( ctxt->fd )  )
        {
            sprintf( ctxt->msgbuff, "fdatasync() failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#else /* macOS */
        if (  fcntl( ctxt->fd, F_FULLFSYNC, 0 ) == -1  )
        {
            sprintf( ctxt->msgbuff, "fcntl( ..., F_FULLFSYNC, ...) failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#endif /* macOS */
        stopus = getTimeAsUs();
        ctxt->fsyncus = stopus - startus;
    }

    if (  stopReceived()  )
        return RET_INTR;

    if (  doclose )
    {
        startus = getTimeAsUs();
        close( ctxt->fd );
        stopus = getTimeAsUs();
        ctxt->fd = -1;
        ctxt->closeus = stopus - startus;
    }

    ctxt->uscrstop = getTimeAsUs();
    ctxt->crduration = ctxt->uscrstop - ctxt->uscrstart;

    return 0;
} // generateFile

/*
 * Generate a random block offset within the test file.
 */

long
getRandomOffset(
                context_t * ctxt
               )
{
    double rval;

    rval = ( (double)rand() * (double)ctxt->maxblock ) / (double)RAND_MAX;

    return ctxt->iosz * (long)rval;
} // getRandomOffset

/*
 * Perform the random I/O test.
 */

int
testIOPSRandom(
               context_t * ctxt,
               int         readops,
               int         doclose
              )
{
    int measuring = 0, done;
    long iooffset, startus, stopus;
    off_t res;
    ssize_t nbytes;

    if (  ctxt->tstate == MEASURE  )
    {
        measuring = 1;
        if (  readops )
            ctxt->usrdstart = getTimeAsUs();
        else
            ctxt->uswrstart = getTimeAsUs();
    }
    done = 0;

    while ( ! done )
    {
        iooffset = getRandomOffset( ctxt );
        res = lseek( ctxt->fd, (off_t)iooffset, SEEK_SET );
        if (  res != (off_t)iooffset  )
        {
            sprintf( ctxt->msgbuff, "%seek failed for offset %'ld",
                     (ctxt->threads>1)?"s":"S", iooffset );
            return 1;
        }
        if (  readops  )
        {
            if (  measuring  )
                ctxt->nreads++;
            errno = 0;
            nbytes = read( ctxt->fd, ctxt->ioblk, (size_t)ctxt->iosz );
            if (  nbytes != ctxt->iosz  )
            {
                sprintf( ctxt->msgbuff, 
                         "%sead failed at offset %'ld - %d (%s)",
                         (ctxt->threads>1)?"r":"R", iooffset, errno, strerror(errno) );
                return 1;
            }
        }
        else
        {
            if (  measuring  )
                ctxt->nwrites++;
            errno = 0;
            nbytes = write( ctxt->fd, ctxt->ioblk, (size_t)ctxt->iosz );
            if (  nbytes != ctxt->iosz  )
            {
                sprintf( ctxt->msgbuff, 
                         "%srite failed at offset %'ld - %d (%s)",
                         (ctxt->threads>1)?"w":"W", iooffset, errno, strerror(errno) );
                return 1;
            }
        }

        if (  ( ctxt->tstate == STOP ) || ( ctxt->tstate == END )  )
        {
            if (  readops  )
            {
                if (  ctxt->usrdstart && ! ctxt->usrdstop  )
                    ctxt->usrdstop = getTimeAsUs();
            }
            else
            {
                if (  ctxt->uswrstart && ! ctxt->uswrstop  )
                    ctxt->uswrstop = getTimeAsUs();
            }
            measuring = 0;
            done = 1;
        }
        else
        if (  ctxt->tstate == RAMP  )
        {
            if (  measuring  )
            {
                if (  readops  )
                {
                    if (  ctxt->usrdstart && ! ctxt->usrdstop  )
                        ctxt->usrdstop = getTimeAsUs();
                }
                else
                {
                    if (  ctxt->uswrstart && ! ctxt->uswrstop  )
                        ctxt->uswrstop = getTimeAsUs();
                }
                measuring = 0;
            }
        }
        else
        if (  ctxt->tstate == MEASURE  )
        {
            if ( ! measuring )
            {
                if (  readops )
                    ctxt->usrdstart = getTimeAsUs();
                else
                    ctxt->uswrstart = getTimeAsUs();
                measuring = 1;
            }
        }
    }

    if (  ! readops && ctxt->nodsync && ! ctxt->nofsync  )
    {
        startus = getTimeAsUs();
        errno = 0;
#if defined(LINUX)
        if (  fdatasync( ctxt->fd )  )
        {
            sprintf( ctxt->msgbuff, "fdatasync() failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#else /* macOS */
        if (  fcntl( ctxt->fd, F_FULLFSYNC, 0 ) == -1  )
        {
            sprintf( ctxt->msgbuff, "fcntl( ..., F_FULLFSYNC, ...) failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#endif /* macOS */
        stopus = getTimeAsUs();
        ctxt->fsyncus = stopus - startus;
    }
    else
        ctxt->fsyncus = 0;

    if (  doclose  )
    {
            startus = getTimeAsUs();
            close( ctxt->fd );
            stopus = getTimeAsUs();
            ctxt->closeus = stopus - startus;
            ctxt->fd = -1;
    }
    else
        ctxt->closeus = 0;

    if (  readops  )
        ctxt->rdduration = ctxt->usrdstop - ctxt->usrdstart;
    else
        ctxt->wrduration = (ctxt->uswrstop - ctxt->uswrstart) + ctxt->fsyncus + ctxt->closeus;

    return 0;
} // testIOPSRandom

/*
 * Perform the sequential I/O test.
 */

int
testIOPSSequential(
                   context_t * ctxt,
                   int         readops,
                   int         doclose
                  )
{
    int measuring = 0, done;
    long iooffset, startus, stopus;
    off_t res;
    ssize_t nbytes;

    // position to start of file
    iooffset = 0;
    res = lseek( ctxt->fd, (off_t)iooffset, SEEK_SET );
    if (  res != (off_t)iooffset  )
    {
        sprintf( ctxt->msgbuff, "%seek failed for offset %'ld",
                 (ctxt->threads>1)?"s":"S", iooffset );
        return 1;
    }

    // perform test
    if (  ctxt->tstate == MEASURE  )
    {
        measuring = 1;
        if (  readops )
            ctxt->usrdstart = getTimeAsUs();
        else
            ctxt->uswrstart = getTimeAsUs();
    }
    done = 0;

    do {
        if (  readops  )
        {
            if (  measuring  )
                ctxt->nreads++;
            nbytes = read( ctxt->fd, ctxt->ioblk, (size_t)ctxt->iosz );
        }
        else
        {
            if (  measuring  )
                ctxt->nwrites++;
            nbytes = write( ctxt->fd, ctxt->ioblk, (size_t)ctxt->iosz );
        }
        iooffset += ctxt->iosz;
        if (  (nbytes == 0) || (iooffset >= ctxt->fsz)  )
        { // wrap to beginning of file
            iooffset = 0;
            res = lseek( ctxt->fd, (off_t)iooffset, SEEK_SET );
            if (  res != (off_t)iooffset  )
            {
                sprintf( ctxt->msgbuff, "%seek failed for offset %'ld",
                         (ctxt->threads>1)?"s":"S", iooffset );
                return 1;
            }

        }

        if (  ( ctxt->tstate == STOP ) || ( ctxt->tstate == END )  )
        {
            if (  readops  )
            {
                if (  ctxt->usrdstart && ! ctxt->usrdstop  )
                    ctxt->usrdstop = getTimeAsUs();
            }
            else
            {
                if (  ctxt->uswrstart && ! ctxt->uswrstop  )
                    ctxt->uswrstop = getTimeAsUs();
            }
            measuring = 0;
            done = 1;
        }
        else
        if (  ctxt->tstate == RAMP  )
        {
            if (  measuring  )
            {
                if (  readops  )
                {
                    if (  ctxt->usrdstart && ! ctxt->usrdstop  )
                        ctxt->usrdstop = getTimeAsUs();
                }
                else
                {
                    if (  ctxt->uswrstart && ! ctxt->uswrstop  )
                        ctxt->uswrstop = getTimeAsUs();
                }
                measuring = 0;
            }
        }
        else
        if (  ctxt->tstate == MEASURE  )
        {
            if ( ! measuring )
            {
                if (  readops )
                    ctxt->usrdstart = getTimeAsUs();
                else
                    ctxt->uswrstart = getTimeAsUs();
                measuring = 1;
            }
        }
    } while (  ! done  );

    if (  nbytes != ctxt->iosz  )
    {
        if (  nbytes < 0  )
        {
            if (  readops  )
                sprintf( ctxt->msgbuff, "%sead failed at offset %'ld",
                         (ctxt->threads>1)?"r":"R", iooffset );
            else
                sprintf( ctxt->msgbuff, "%srite failed at offset %'ld",
                         (ctxt->threads>1)?"w":"W", iooffset );
            return 1;
        }
        else
        {
            if (  readops  )
                sprintf( ctxt->msgbuff, "%short read (%'ld) at offset %'ld",
                         (ctxt->threads>1)?"s":"S", (long)nbytes, iooffset );
            else
                sprintf( ctxt->msgbuff, "%short write (%'ld) at offset %'ld",
                         (ctxt->threads>1)?"s":"S", (long)nbytes, iooffset );
            return 1;
        }
    }

    if (  ! readops && ctxt->nodsync && ! ctxt->nofsync  )
    {
        startus = getTimeAsUs();
        errno = 0;
#if defined(LINUX)
        if (  fdatasync( ctxt->fd )  )
        {
            sprintf( ctxt->msgbuff, "fdatasync() failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#else /* macOS */
        if (  fcntl( ctxt->fd, F_FULLFSYNC, 0 ) == -1  )
        {
            sprintf( ctxt->msgbuff, "fcntl( ..., F_FULLFSYNC, ...) failed: %d (%s)",
                     errno, strerror( errno )  );
            return 1;
        }
#endif /* macOS */
        stopus = getTimeAsUs();
        ctxt->fsyncus = stopus - startus;
    }
    else
        ctxt->fsyncus = 0;
 
    if (  doclose  )
    {
            startus = getTimeAsUs();
            close( ctxt->fd );
            stopus = getTimeAsUs();
            ctxt->closeus = stopus - startus;
            ctxt->fd = -1;
    }
    else
        ctxt->closeus = 0;

    if (  readops  )
	ctxt->rdduration = ctxt->usrdstop - ctxt->usrdstart;
    else
        ctxt->wrduration = (ctxt->uswrstop - ctxt->uswrstart) + ctxt->fsyncus + ctxt->closeus;

    return 0;
} // testIOPSSequential

/*
 * The test execution thread.
 */

void *
testThread(
           void * arg
          )
{
    int ret = 0;
    context_t * ctxt = (context_t *)arg;

    if (  arg == NULL  )
        return NULL;


    // Generate test file

    // indicate ready
    ctxt->crready = 1;
    ctxt->retcode = -1;

    if (  ! ctxt->usrfile  )
    {
        // wait for start
        while (  ! ctxt->crstart  )
        {
            if (  ctxt->tstate == STOP  )
            {
                ctxt->rdfinished = ctxt->wrfinished = ctxt->crfinished = -1;
                ctxt->retcode = RET_INTR;
                return NULL;
            }
            usSleep( WAIT_US );
        }
        // generate file
        if (  ! ctxt->onefile || (ctxt->threadno == 0)  )
            ret = generateFile( ctxt, 0 );
        if (  ret == RET_INTR  )
        {
            ctxt->retcode = RET_INTR;
            ctxt->crfinished = 1;
            ctxt->rdfinished = ctxt->wrfinished = 1;
            return NULL;
        }
        else
        if (  ret  )
        {
            ctxt->rdfinished = ctxt->wrfinished = ctxt->crfinished = -1;
            return NULL;
        }
    }
    ctxt->crfinished = 1;

    // Test read IOPS
    // indicate ready
    ctxt->rdready = 1;
    if (  ! ctxt->noread && (ctxt->rdstart == 0)  )
    {
        // wait for start
        while (  ! ctxt->rdstart  )
        {
            if (  ctxt->tstate == STOP  )
            {
                ctxt->retcode = RET_INTR;
                ctxt->rdfinished = ctxt->wrfinished = -1;
                return NULL;
            }
            usSleep( WAIT_US );
        }
        // test IOPS
        if (  ctxt->testmode == MODE_SEQUENTIAL  )
            ret = testIOPSSequential( ctxt, 1, 0 );
        else
            ret = testIOPSRandom( ctxt, 1, 0 );
        if (  ret == RET_INTR  )
        {
            ctxt->retcode = RET_INTR;
            ctxt->rdfinished = 1;
            ctxt->wrfinished = -1;
            return NULL;
        }
        else
        if (  ret  )
        {
            ctxt->rdfinished = ctxt->wrfinished = -1;
            return NULL;
        }
    }
    ctxt->rdfinished = 1;

    // Test write IOPS
    // indicate ready
    ctxt->wrready = 1;
    if (  ! ctxt->nowrite && (ctxt->wrstart == 0)  )
    {
        // wait for start
        while (  ! ctxt->wrstart  )
        {
            if (  ctxt->tstate == STOP  )
            {
                ctxt->wrfinished = -1;
                return NULL;
            }
            usSleep( WAIT_US );
        }
        // test IOPS
        if (  ctxt->testmode == MODE_SEQUENTIAL  )
            ret = testIOPSSequential( ctxt, 0, 1 );
        else
            ret = testIOPSRandom( ctxt, 0, 1 );
        if (  ret == RET_INTR  )
        {
            ctxt->retcode = RET_INTR;
            ctxt->wrfinished = 1;
            return NULL;
        }
        else
        if (  ret  )
        {
            ctxt->wrfinished = -1;
            return NULL;
        }
    }
    ctxt->wrfinished = 1;
    ctxt->retcode = 0;

    return NULL;
} // testThread

/*
 * Test thread coordinator.
 */

int
runTests(
         context_t * mainctxt,
         context_t   threadcontexts[],
         int         numcontexts
        )
{
    int i, allready, haderror;
    long usdur, minstart, minstop, maxstart, maxstop;
    long rlimit, dlimit, now;
    int ramping;
    tstate_t tstate, pstate;
    double duration;
    char * fmt = NULL;

    // create all threads
    for ( i = 0; i < numcontexts; i++ )
    {
        if (  pthread_create( &threadcontexts[i].tid, NULL, testThread,
                              (void *)&threadcontexts[i] )  )
        {
            fprintf( stderr, "*** Unable to start thread %d\n", i+1  );
            return 2;
        }
        threadcontexts[i].tstate = RUNNING;
    }

    // wait for them to be ready
    do {
        allready = 1;
        for ( i = 0; i < numcontexts; i++ )
            if (  ! threadcontexts[i].crready  )
                allready = 0;
        if (  ! allready  )
            usSleep( WAIT_US );
    } while ( ! allready );

    if (  ! mainctxt->usrfile  )
    {
        if (  mainctxt->onefile || (numcontexts == 1)  )
            printf("Generating test file of size %'ld bytes...\n", mctxt.fsz);
        else
            printf("Generating %d test files each of size %'ld bytes...\n", 
                   numcontexts, mctxt.fsz);

        gettimeofday( &pstart, NULL );
        getrusage( RUSAGE_SELF, &rstart );

        // Tell them all to start file creation
        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].crstart = 1;

        // wait for them all to finish file creation
        do {
            allready = 1;
            for ( i = 0; i < numcontexts; i++ )
                if (  ! threadcontexts[i].crfinished  )
                    allready = 0;
            if (  ! allready  )
                usSleep( WAIT_US );
        } while ( ! allready );

        getrusage( RUSAGE_SELF, &rend );
        gettimeofday( &pend, NULL );

        // check for errors
        haderror = 0;
        for ( i = 0; i < numcontexts; i++ )
            if (  threadcontexts[i].crfinished < 1  )
            {
                haderror = 1;
                if (  numcontexts > 1  )
                    fprintf( stderr, "*** Thread %d: %s\n",
                             i, threadcontexts[i].msgbuff  );
                else
                    fprintf( stderr, "*** %s\n", threadcontexts[i].msgbuff  );
            }
        if (  haderror  )
        {
            // abort all threads
            for ( i = 0; i < numcontexts; i++ )
                threadcontexts[i].rdstart = threadcontexts[i].wrstart = -1;
            mainctxt->crfinished = -1;
            return 3;
        }
    
        // summarise and report results
        maxstart = maxstop = mainctxt->fsz = 0L;
        minstart = minstop = 999999999999999999L;
        for ( i = 0; i < numcontexts; i++ )
        {
            if (  threadcontexts[i].uscrstart > maxstart  )
                maxstart = threadcontexts[i].uscrstart;
            if (  threadcontexts[i].uscrstop > maxstop  )
                maxstop = threadcontexts[i].uscrstop;
            if (  threadcontexts[i].uscrstart < minstart  )
                minstart = threadcontexts[i].uscrstart;
            if (  threadcontexts[i].uscrstop < minstop  )
                minstop = threadcontexts[i].uscrstop;
            usdur = threadcontexts[i].crduration;
            mainctxt->crduration += usdur;
            mainctxt->fsz += threadcontexts[i].fsz;
            mainctxt->preallocus += threadcontexts[i].preallocus;
            mainctxt->fsyncus += threadcontexts[i].fsyncus;
            mainctxt->closeus += threadcontexts[i].closeus;
            if (  mainctxt->verbose && (mainctxt->threads > 1)  )
            {
                if (  ! mainctxt->onefile || (i == 0)  )
                {
                    if (  (mainctxt->fsz >= MB_MULT) && (usdur > 100000L)  )
                    {
                        printf("Thread %d: write time = %'ld µs, rate = %.2f MB/s\n", i, usdur,
             (((double)threadcontexts[i].fsz/(double)MB_MULT)*1000000.0)/(double)usdur );
                        if (  threadcontexts[i].fsyncus )
                            printf("Thread %d: sync time = %'ld µs\n", i, threadcontexts[i].fsyncus );
                        if (  threadcontexts[i].closeus )
                            printf("Thread %d: close time = %'ld µs\n", i, threadcontexts[i].closeus );
                    }
                    else
                        printf("Thread %d: insufficient accuracy to report write rate\n", i );
                }
            }
        }
        mainctxt->crduration /= numcontexts;
    
        if (  mainctxt->preallocus  )
        {
            if (  mainctxt->onefile || (mainctxt->threads == 1)  )
                printf("Preallocation time = %'ld µs\n", mainctxt->preallocus );
            else
                printf("Average preallocation time = %'ld µs\n", mainctxt->preallocus / mainctxt->threads );
        }
        if (  (mainctxt->crduration >= 100000) &&
              (mainctxt->fsz >= MB_MULT)  )
        {
            if (  mainctxt->onefile || (mainctxt->threads == 1)  )
                fmt = "Write time = %'ld µs, rate = %.2f MB/s\n";
            else
                fmt = "Average write time = %'ld µs, aggregate write rate = %.2f MB/s\n";
            printf(fmt, mainctxt->crduration,
         (((double)mainctxt->fsz/(double)MB_MULT)*1000000.0)/(double)mainctxt->crduration );
            if (  (mainctxt->threads > 1) && ! mainctxt->onefile  )
                printf("Measurement variation: start = %'ld µs, stop = %'ld µs\n",
                       (maxstart - minstart), (maxstop - minstop) );
            if (  mainctxt->fsyncus  )
            {
                if (  mainctxt->onefile || (mainctxt->threads == 1)  )
                    printf("Sync time = %'ld µs\n", mainctxt->fsyncus );
                else
                    printf("Average sync time = %'ld µs\n", mainctxt->fsyncus / mainctxt->threads );
            }
            if (  mainctxt->closeus  )
            {
                if (  mainctxt->onefile || (mainctxt->threads == 1)  )
                    printf("Close time = %'ld µs\n", mainctxt->closeus );
                else
                    printf("Average close time = %'ld µs\n", mainctxt->closeus / mainctxt->threads );
            }
        }
        else
            printf("Insufficient accuracy to report write rate\n");
        printf( "\n" );
        if (  mainctxt->reportcpu  )
        {
            reportTimes();
            printf( "\n" );
        }

        if (  stopReceived()  )
        {
            for ( i = 0; i < numcontexts; i++ )
                threadcontexts[i].tstate = STOP;
            goto fini;
        }
    
    } // usrfile
    
    // Tell them all to start read test
    if (  ! mainctxt->noread  )
    {
        printf("Testing reads...\n");

        ramping = (mainctxt->ramp > 0);
        now = getTimeAsUs();
        if (  ramping  )
        {
            rlimit = now + (mainctxt->ramp * 1000000);
            dlimit = 0;
            tstate = pstate = RAMP;
        }
        else
        {
            dlimit = now + (mainctxt->duration * 1000000);
            rlimit = 0;
            tstate = pstate = MEASURE;
            gettimeofday( &pstart, NULL );
            getrusage( RUSAGE_SELF, &rstart );
        }

        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].tstate = tstate;
        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].rdstart = 1;

        // wait for them all to finish read test
        do {

            now = getTimeAsUs();
            if (  stopReceived()  )
            {
                tstate = STOP;
                if (  dlimit && ! ramping  )
                {
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
            }
            else
            if (  ramping  )
            {
                if (  now > rlimit  )
                {
                    if (  dlimit == 0  )
                    {
                        ramping = 0;
                        dlimit = now + (mainctxt->duration * 1000000);
                        tstate = MEASURE;
                        gettimeofday( &pstart, NULL );
                        getrusage( RUSAGE_SELF, &rstart );
                    }
                    else
                        tstate = END;
                }
            }
            else
            if (  now > dlimit  )
            {
                if (  rlimit != 0  )
                {
                    ramping = 1;
                    rlimit = now + (mainctxt->ramp * 1000000);
                    tstate = RAMP;
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
                else
                {
                    tstate = END;
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
            }

            if (  tstate != pstate  )
            {
                for ( i = 0; i < numcontexts; i++ )
                    threadcontexts[i].tstate = tstate;
                pstate = tstate;
            }

            allready = 1;
            for ( i = 0; i < numcontexts; i++ )
                if (  ! threadcontexts[i].rdfinished  )
                    allready = 0;
            if (  ! allready  )
                usSleep( WAIT_US );
        } while ( ! allready );

        // check for errors
        haderror = 0;
        for ( i = 0; i < numcontexts; i++ )
            if (  threadcontexts[i].rdfinished < 0  )
            {
                haderror = 1;
                if (  numcontexts > 1  )
                    fprintf( stderr, "*** Thread %d: %s\n",
                             i, threadcontexts[i].msgbuff  );
                else
                    fprintf( stderr, "*** %s\n", threadcontexts[i].msgbuff  );
            }
        if (  haderror  )
        {
            // abort all threads
            for ( i = 0; i < numcontexts; i++ )
                threadcontexts[i].wrstart = -1;
            // mainctxt->rdfinished = -1;
            return 4;
        }

        // summarise and report results
        maxstart = maxstop = 0L;
        minstart = minstop = 999999999999999999L;
        mainctxt->fsyncus = 0;
        for ( i = 0; i < numcontexts; i++ )
        {
            if (  threadcontexts[i].usrdstart > maxstart  )
                maxstart = threadcontexts[i].usrdstart;
            if (  threadcontexts[i].usrdstop > maxstop  )
                maxstop = threadcontexts[i].usrdstop;
            if (  threadcontexts[i].usrdstart < minstart  )
                minstart = threadcontexts[i].usrdstart;
            if (  threadcontexts[i].usrdstop < minstop  )
                minstop = threadcontexts[i].usrdstop;
            mainctxt->nreads += threadcontexts[i].nreads;
            usdur = threadcontexts[i].rdduration;
            mainctxt->rdduration += usdur;
            if (  mainctxt->verbose && (mainctxt->threads > 1) && (usdur > 0)  )
                printf("Thread %d: %'ld reads in %'ld µs = %.2f read IOPS, %.2f MB/s\n",
                   i, threadcontexts[i].nreads, usdur,
          ((double)threadcontexts[i].nreads*(double)1000000.0)/(double)usdur,
          ((double)threadcontexts[i].nreads*(double)threadcontexts[i].iosz*(double)1000000.0)/(double)(MB_MULT*usdur));
        }

        mainctxt->rdduration /= numcontexts;
        if (  mainctxt->rdduration > 0  )
        {
            printf("\n%'ld total reads in %.3f seconds = %.2f read IOPS, %.2f MB/s\n", 
                   mainctxt->nreads, (double)mainctxt->rdduration / 1000000.0,
          ((double)mainctxt->nreads*(double)1000000.0)/(double)mainctxt->rdduration,
          ((double)mainctxt->nreads*(double)mainctxt->iosz*(double)1000000.0)/(double)(MB_MULT * mainctxt->rdduration));
        if (  mainctxt->threads > 1  )
            printf("Measurement variation: start = %'ld µs, stop = %'ld µs\n",
                   (maxstart - minstart), (maxstop - minstop) );
            printf("\n");
            if (  mainctxt->reportcpu  )
            {
                reportTimes();
                printf( "\n" );
            }
        }
    }

    if (  stopReceived()  )
    {
        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].tstate = STOP;
        goto fini;
    }
    
    // Tell them all to start write test
    if (  ! mainctxt->nowrite  )
    {
        printf("Testing writes...\n");

        ramping = (mainctxt->ramp > 0);
        now = getTimeAsUs();
        if (  ramping  )
        {
            rlimit = now + (mainctxt->ramp * 1000000);
            dlimit = 0;
            tstate = pstate = RAMP;
        }
        else
        {
            dlimit = now + (mainctxt->duration * 1000000);
            rlimit = 0;
            tstate = pstate = MEASURE;
            gettimeofday( &pstart, NULL );
            getrusage( RUSAGE_SELF, &rstart );
        }
        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].tstate = tstate;

        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].wrstart = 1;

        // wait for them all to finish write test
        do {
            now = getTimeAsUs();
            if (  stopReceived()  )
            {
                tstate = STOP;
                if (  dlimit && ! ramping  )
                {
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
            }
            else
            if (  ramping  )
            {
                if (  now > rlimit  )
                {
                    if (  dlimit == 0  )
                    {
                        ramping = 0;
                        dlimit = now + (mainctxt->duration * 1000000);
                        tstate = MEASURE;
                        gettimeofday( &pstart, NULL );
                        getrusage( RUSAGE_SELF, &rstart );
                    }
                    else
                        tstate = END;
                }
            }
            else
            if (  now > dlimit  )
            {
                if (  rlimit != 0  )
                {
                    ramping = 1;
                    rlimit = now + (mainctxt->ramp * 1000000);
                    tstate = RAMP;
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
                else
                {
                    tstate = END;
                    getrusage( RUSAGE_SELF, &rend );
                    gettimeofday( &pend, NULL );
                }
            }

            if (  tstate != pstate  )
            {
                for ( i = 0; i < numcontexts; i++ )
                    threadcontexts[i].tstate = tstate;
                pstate = tstate;
            }

            allready = 1;
            for ( i = 0; i < numcontexts; i++ )
                if (  ! threadcontexts[i].wrfinished  )
                    allready = 0;
            if (  ! allready  )
                usSleep( WAIT_US );
        } while ( ! allready );

        // check for errors
        haderror = 0;
        for ( i = 0; i < numcontexts; i++ )
            if (  threadcontexts[i].wrfinished < 0  )
            {
                haderror = 1;
                if (  numcontexts > 1  )
                    fprintf( stderr, "*** Thread %d: %s\n",
                             i, threadcontexts[i].msgbuff  );
                else
                    fprintf( stderr, "*** %s\n", threadcontexts[i].msgbuff  );
            }
        if (  haderror  )
        {
            mainctxt->wrfinished = -1;
            return 5;
        }
    
        // summarise and report results
        maxstart = maxstop = 0L;
        minstart = minstop = 999999999999999999L;
        mainctxt->fsyncus = 0;
        for ( i = 0; i < numcontexts; i++ )
        {
            if (  threadcontexts[i].uswrstart > maxstart  )
                maxstart = threadcontexts[i].uswrstart;
            if (  threadcontexts[i].uswrstop > maxstop  )
                maxstop = threadcontexts[i].uswrstop;
            if (  threadcontexts[i].uswrstart < minstart  )
                minstart = threadcontexts[i].uswrstart;
            if (  threadcontexts[i].uswrstop < minstop  )
                minstop = threadcontexts[i].uswrstop;
            mainctxt->nwrites += threadcontexts[i].nwrites;
            usdur = threadcontexts[i].wrduration;
            mainctxt->wrduration += usdur;
            mainctxt->fsyncus += threadcontexts[i].fsyncus;
            mainctxt->closeus += threadcontexts[i].closeus;
            if (  mainctxt->verbose && (mainctxt->threads > 1) &&
                  (threadcontexts[i].wrduration > 0)  )
            {
                printf("Thread %d: %'ld writes in %'ld µs = %.2f write IOPS, %.2f MB/s\n",
                   i, threadcontexts[i].nwrites, usdur,
          ((double)threadcontexts[i].nwrites*(double)1000000.0)/(double)usdur,
          ((double)threadcontexts[i].nwrites*(double)threadcontexts[i].iosz*(double)1000000.0)/(double)(MB_MULT*usdur));
                if (  threadcontexts[i].fsyncus  )
                    printf("Thread %d: sync time = %'ld µs\n", i, threadcontexts[i].fsyncus );
                if (  threadcontexts[i].closeus  )
                    printf("Thread %d: close time = %'ld µs\n", i, threadcontexts[i].closeus );
            }
        }

        mainctxt->wrduration /= numcontexts;
        if (  mainctxt->wrduration > 0  )
        {
            {
              printf("\n%'ld total writes in %.3f seconds = %.2f write IOPS, %.2f MB/s\n", 
                       mainctxt->nwrites, (double)mainctxt->wrduration / 1000000.0, 
              ((double)mainctxt->nwrites*(double)1000000.0)/(double)mainctxt->wrduration,
              ((double)mainctxt->nwrites*(double)mainctxt->iosz*(double)1000000.0)/(double)(MB_MULT*mainctxt->wrduration));
              if ( mainctxt->fsyncus )
              {
                  if ( mainctxt->threads == 1  )
                      printf("Sync time = %'ld µs\n", mainctxt->fsyncus );
                  else
                      printf("Average sync time = %'ld µs\n", mainctxt->fsyncus / mainctxt->threads );
              }
              if ( mainctxt->closeus )
              {
                  if ( mainctxt->threads == 1  )
                      printf("Close time = %'ld µs\n", mainctxt->closeus );
                  else
                      printf("Average close time = %'ld µs\n", mainctxt->closeus / mainctxt->threads );
              }
            }
            if (  mainctxt->threads > 1  )
                printf("Measurement variation: start = %'ld µs, stop = %'ld µs\n",
                       (maxstart - minstart), (maxstop - minstop) );
            printf("\n");
            if (  mainctxt->reportcpu  )
            {
                reportTimes();
                printf( "\n" );
            }
        }
    }

    if (  stopReceived()  )
    {
        for ( i = 0; i < numcontexts; i++ )
            threadcontexts[i].tstate = STOP;
    }

fini:
    // cleanup all threads
    for ( i = 0; i < numcontexts; i++ )
        pthread_join( threadcontexts[i].tid, NULL );
    
    return 0;
} // runTests

/*
 * Create a test file (CREATE mode).
 */

int
createFile(
           context_t * ctxt
          )
{
    int ret = 0;

    if (  (ret = initTests( ctxt )) == 0  )
    {
        if (  ctxt->nopreallocate  )
            printf("Preallocation is disabled\n\n");
        printf("Filesystem block size is %'ld bytes\n", ctxt->blksz);
#if ! defined(LINUX)
        printf("Filesystem optimal I/O size is %'ld bytes\n", ctxt->optiosz);
#endif /* LINUX */
        printf("\nFile generation block size is %'ld bytes\n\n", ctxt->geniosz);

        gettimeofday( &pstart, NULL );
        getrusage( RUSAGE_SELF, &rstart );
        ret = generateFile( ctxt, 1 );
        getrusage( RUSAGE_SELF, &rend );
        gettimeofday( &pend, NULL );

        if (  ret == 0  )
        {
            // report results
            if (  ctxt->preallocus  )
                printf("Preallocation time = %'ld µs\n", ctxt->preallocus );
            if (  (ctxt->crduration >= 100000) && (ctxt->fsz >= MB_MULT)  )
            {
                printf("Write time = %'ld µs, rate = %.2f MB/s\n", ctxt->crduration,
             (((double)ctxt->fsz/(double)MB_MULT)*1000000.0)/(double)ctxt->crduration );
                if (  ctxt->fsyncus  )
                    printf("Sync time = %'ld µs\n", ctxt->fsyncus );
                if (  ctxt->closeus  )
                    printf("Close time = %'ld µs\n", ctxt->closeus );
            }
            else
                printf("Insufficient accuracy to report write rate\n");
            printf("\n");
            if (  ctxt->reportcpu  )
            {
                reportTimes();
                printf( "\n" );
            }
        }
        else
            unlink( ctxt->tfname );
    }

    return ret;
} // createFile

/*
 * Main
 */

int
main(
     int    argc,
     char * argv[]
    )
{
    int ret = 0;
#if defined(ALLOW_RAW) && defined(ALLOW_RAWWRITE)
    char * eval = NULL;

    eval = getenv( ENV_RAWWRITE );
    if (  ( eval != NULL ) && ( strcmp( eval, ENV_RAWVALUE ) == 0 )  )
        mctxt.rawwrite = -1;
#endif /* ALLOW_RAW && ALLOW_RAWWRITE */

    setlocale( LC_ALL, "" );

    if (  argc == 1  )
        usage( 0 );

    if (  (strcmp(argv[1], "s") == 0) ||
          (strcmp(argv[1], "sequential") == 0)  )
        mctxt.testmode = MODE_SEQUENTIAL;
    else
    if (  (strcmp(argv[1], "r") == 0) ||
          (strcmp(argv[1], "random") == 0)  )
        mctxt.testmode = MODE_RANDOM;
    else
    if (  (strcmp(argv[1], "c") == 0) ||
          (strcmp(argv[1], "create") == 0)  )
        mctxt.testmode = MODE_CREATE;
    else
    if (  (strcmp( argv[1], "h" ) == 0) || 
          (strcmp( argv[1], "help" ) == 0)  )
        usage( 1 );
    else
        usage( 0 );

    if (  parseArgs( argc, 2, argv, &mctxt )  )
        usage( 0 );

    setbuf( stdout, NULL );

    handleSignals();

    printf("\n----------------------------------------------------------------------\n\n");

    printf("%s version %s\n\n", PROGNAME, VERSION );

    if (  mctxt.testmode == MODE_CREATE  )
        ret = createFile( &mctxt );
    else
    {
        if (  mctxt.testmode == MODE_SEQUENTIAL  )
            printf("Sequential mode\n");
        else
            printf("Random mode\n");
        if (  mctxt.onefile  )
        {
            if (  mctxt.usrfile  )
                printf("User file mode\n");
            else
                printf("Single file mode\n");
        }
        printf("Path '%s'\n", mctxt.fname );
        printf("%d thread%s\n", mctxt.threads, (mctxt.threads>1)?"s":"" );
        if (  mctxt.nopreallocate  )
            printf("Preallocation is disabled\n");
        if (  mctxt.rdahead  )
            printf("Read ahead is not disabled\n");
        if (  mctxt.cache  )
            printf("Filesystem cache is not disabled\n");
        if (  mctxt.nodsync  )
            printf("O_DSYNC is not used\n");
        if (  mctxt.nofsync  )
            printf("fdatasync() is not used\n");
    
        if (  (ret = initContexts( &mctxt, tctxt, mctxt.threads )) == 0  )
        {
#if defined( ALLOW_RAW )
            printf("\n%s block size is %'ld bytes\n", mctxt.raw?"Device":"Filesystem", mctxt.blksz);
#if ! defined(LINUX)
            printf("%s optimal I/O size is %'ld bytes\n", mctxt.raw?"Device":"Filesystem", mctxt.optiosz);
#endif /* LINUX */
#else /* ! ALLOW_RAW */
            printf("\nFilesystem block size is %'ld bytes\n", mctxt.blksz);
#if ! defined(LINUX)
            printf("Filesystem optimal I/O size is %'ld bytes\n", mctxt.optiosz);
#endif /* LINUX */
#endif /* ! ALLOW_RAW */
            if (  ! mctxt.usrfile  )
                printf("\nFile generation block size is %'ld bytes\n", mctxt.geniosz);
            printf("\nTest block size is %'ld bytes\n\n", mctxt.iosz);
    
            ret = runTests( &mctxt, tctxt, mctxt.threads );
        }
    
        cleanupContexts( tctxt, mctxt.threads );
    }
    
    return ret;
} // main

