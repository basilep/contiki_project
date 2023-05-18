CONTIKI_PROJECT = nullcat_training.c
PROJECT_SOURCEFILES = realloc.c
all: $(CONTIKI_PROJECT)

CONTIKI = ../..

#use this to enable TSCH: MAKE_MAC = MAKE_MAC_TSCH
MAKE_MAC ?= MAKE_MAC_CSMA
MAKE_NET = MAKE_NET_NULLNET
include $(CONTIKI)/Makefile.include
