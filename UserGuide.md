## statfs

Usage:

   statfs [-s | -r] [path]

If 'path' is provided, displays filesystem information for the
specified path otherwise displays filesystem information for all
mounted filesystems.

By default the information is displayed in a human friendly format.
If '-r' is specified the information is displayed in raw format in
the same order as the as the definition of the 'statfs' structure.
If '-s' is specified then the output is in 'shell' format suitable
for use with 'eval'.

NOTES:  
    - The 'path' argument is not optional on Linux
    - The 'optimal I/O size' datum is not displayed on Linux (since Linux does not provide accurate information for that item)

## iops


Usage:

   iops { s[equential] | r[andom] } [-file <fpath>] [-fsize <fsz>] [-cpu]
        [-iosz <tsz>] [-dur <tdur>] [-ramp <tramp>] [-noread | -nowrite]
        [-geniosz <gsz>] [-threads <nthr>] [-verbose] [-1file [<usrfpath>]]
        [-nopreallocate] [-rdahead] [-cache] [-nodysnc [-nofsync]]

   iops c[reate] [-file <fpath>] [-fsize <fsz>] [-geniosz <gsz>]
        [-nopreallocate] [-cpu]

   iops h[elp]

Tests the I/O capability of storage devices, filesystems and OS.

Parameters are:

  s[equential]
     Performs a sequential I/O test. The test file(s) are read and
     written sequentially.

  r[andom]
     Performs a random I/O test. The test file(s) are read and written
     randomly.

  c[reate]
     Creates a file suitable for later use with the '-1file' option.

  h[elp]
     Display full help (this text).

   fpath  -  Path of the file to use for testing. A separate file
             named 'fpath-nn' will be created for each test thread
             where 'nn' is the thread number (unless the '-1file'
             option is used). These files must not already exist.
             The default value for 'fpath' is 'iopsdata'.

   fsz    -  This size of each test file. By default the size
             is specified in bytes but it can be specified in
             kilobytes (1024 bytes), megabytes (1024\*1024 bytes) or
             gigabytes (1024\*1024\*1024 bytes) by using a suffix of
             k, m or g on the value. The value must be in the
             range 1 GB to 10 GB. The default is 1 GB.

   tsz     -  The size of each test I/O request, specified in the
              same manner as for 'fsz'. The value must be > 0 and
              <= 32 MB. The default is the optimal I/O size for
              the file system containing the test file or 1 MB
              if that cannot be determined.

   gsz     -  The size of each write request when creating the test
              file(s), specified in the same manner as for 'fsz'. Must
              be > 0 and <= 'fsz'. The default is the closest multiple
              of the filesystem's optimal I/O size to 32 MB or 32 MB
              if that cannot be determined.

   tdur    -  The duration of the measured part of the test in seconds.
              Must be between 10 and 3,600, the default is 30

   tramp   -  The ramp up/down time, before/after the measured part of
              the test, in seconds. Must be between 0 and 60,
              the default is 10

   NOTE:
              The default measurement duration and ramp times have been
              chosen to give a reasonable result across a wide range of
              storage systems.

   nthr   -  The number of concurrent threads to use for the test.
              The minimum (and default) value is 1 and the maximum
              is 64. Threads are numbered from 0. With fast
              devices (SSDs and similar) you will likely need to use
              multiple threads in order to accurately measure the
              device's maximum performance. For rotational devices
              (regular HDDs) using multiple threads may be counter
              productive as it could result in contention (though the
              results may still be interesting).

   -cpu       Displays CPU usage information for the measurement part
              of each test.

   -verbose   Displays additional, possibly interesting, information
              during execution. Primarily per thread metrics

The following options are for special usage only. The objective
of this utility is to measure the performance of the hardware (as
far as is possible given that a filesystem is interposed between
the test program and the hardware). As a result certain OS features
are used by default to try to achieve this. The setings below allow
you to change aspects of the default behaviour. This may be interesting
but the results so achieved should be interpreted with caution.

   -1file [usrfpath]
              Normally each test thread creates its own test file in order
              to avoid any filesystem contention that might arise from
              multiple threads performing I/O on the same file. When this
              option is specified all threads share the same test file.
              Each thread opens the file independently but I/O operations
              are not synchronised between the threads.

   Normally the test file is created automatically but if the
   optional 'usrfpath' is used then the specified file is used
   instead.

   NOTES:
   
   - If a user file is specified it must be between 1 GB
    and 10 GB in size.
                
   - The contents of the user file will be overwritten without
     warning!
                
   - The user file will not be removed at the end of the test.
           
   - 'usrfpath' option is mutually exclusive with the
     '-file', '-geniosz' and '-nopreallocate' options.
                
   - The default for '-fsize' is the size of the user file.
     If you explicitly specify a value for '-fsize' it must be
     <= the actual file size.

   -nopreallocate
              Normally the space for the test file(s) is pre-allocated
              (contiguously if possible) using OS APIs. If this
              option is specified then the space will not be pre-
              allocated.

   -rdahead   Normally OS read ahead is disabled for the test
              file(s). If this option is specified then read ahead
              will not be explicitly disabled.

   -cache     Normally OS filesystem caching is disabled for the
              test file(s). If this option is specified then caching
              will not be explicitly disabled.

   -nodsync   Normally the test file(s) are opened with O_DSYNC. If
              this option is specified then that flag will not
              be used.

   -nofsync   If '-nodsync' is specified then at the end of a write
              test each thread will call the platform equivalent of
              fdatasync() on the file. If this option is specified
              then that call is not made.

