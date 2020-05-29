############################################################
#
# Solaris Makefile for IOPS and STATFS
#
# Copyright (c) Chris Jenkins 2019, 2020
#
# Licensed under the Universal Permissive License v 1.0 as
# shown at http://oss.oracle.com/licenses/upl
#
############################################################

all:    iops

clean:
	rm -rf iops *.o

iops:   ../iops.c
	cc -m64 -O2 -DSOLARIS -o iops ../iops.c

