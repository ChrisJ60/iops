############################################################
#
# Linux Makefile for IOPS and STATFS
#
# Copyright (c) Chris Jenkins 2019, 2020
#
# Licensed under the Universal Permissive License v 1.0 as
# shown at http://oss.oracle.com/licenses/upl
#
############################################################

all:	iops statfs rawsz

clean:
	rm -rf iops statfs rawsz *.o

iops:	../iops.c
	gcc -DLINUX -O2 -o iops ../iops.c -lpthread

statfs:	../statfs.c
	gcc -DLINUX -O2 -o statfs ../statfs.c

rawsz:	../rawsz.c
	gcc -DLINUX -O2 -o rawsz ../rawsz.c
