############################################################
#
# macOS Makefile for IOPS and STATFS
#
# Copyright (c) Chris Jenkins 2019, 2020
#
# Licensed under the Universal Permissive License v 1.0 as
# shown at http://oss.oracle.com/licenses/upl
#
############################################################

all:    iops statfs rawsz

clean:
	rm -rf iops statfs rawsz *.o *.dSYM

iops:   ../iops.c
	cc -O2 -DMACOS -o iops ../iops.c

statfs: ../statfs.c
	cc -O2 -DMACOS -o statfs ../statfs.c

rawsz: ../rawsz.c
	cc -O2 -DMACOS -o rawsz ../rawsz.c
