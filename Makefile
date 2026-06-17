
BASEDIR=$(shell pwd)
SRCDIR=$(BASEDIR)/src
BUILDDIR=$(BASEDIR)/build
LIBINSTALLDIR=$(BUILDDIR)/libs
LIBDIR=$(BASEDIR)/libs

SRCS := $(wildcard $(SRCDIR)/*.c)
SRCS += $(wildcard $(SRCDIR)/dtc/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

CFLAGS += -I$(BUILDDIR)/libs/include
CFLAGS += -I$(SRCDIR)
CFLAGS += -Wall

LDFLAGS += -static 
LDFLAGS += -L$(BUILDDIR)/libs/lib
LDFLAGS += -ltar -lfdt -lmd 
#LDFLAGS += -Wl,-Bstatic -ltar -lfdt -lmd -Wl,-Bdynamic

all: main

libmd: $(LIBINSTALLDIR)/.libmd_stamp
$(LIBINSTALLDIR)/.libmd_stamp: | $(BUILDDIR)	
	cd $(LIBDIR)/libmd && autoreconf -i -f
	cd $(LIBDIR)/libmd && ./configure --enable-shared --prefix=$(LIBINSTALLDIR) 
	make -C $(LIBDIR)/libmd 	
	make -C $(LIBDIR)/libmd install
	touch $(LIBINSTALLDIR)/.libmd_stamp

libfdt: $(LIBINSTALLDIR)/.libfdt_stamp
$(LIBINSTALLDIR)/.libfdt_stamp: | $(BUILDDIR)
	make -C $(LIBDIR)/dtc NO_PYTHON=1
	make -C $(LIBDIR)/dtc install PREFIX=$(LIBINSTALLDIR)
	touch $(LIBINSTALLDIR)/.libfdt_stamp

libtar: $(LIBINSTALLDIR)/.libtar_stamp
$(LIBINSTALLDIR)/.libtar_stamp: | $(BUILDDIR) libtar_patch 
	cd $(LIBDIR)/libtar && autoreconf -i -f
	cd $(LIBDIR)/libtar && ./configure --enable-shared --prefix=$(LIBINSTALLDIR) 
	make -C $(LIBDIR)/libtar
	make -C $(LIBDIR)/libtar install
	touch $(LIBINSTALLDIR)/.libtar_stamp

libtar_patch: $(LIBDIR)/libtar/.patched
$(LIBDIR)/libtar/.patched:
	cd $(LIBDIR)/libtar && patch -p1 < $(LIBDIR)/patches/fuck-glibc.patch
	touch $(LIBDIR)/libtar/.patched

main: libfdt libtar libmd $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -r $(BUILDDIR)
	-cd $(LIBDIR)/libtar && git restore . && git clean -dxf	
	-cd $(LIBDIR)/libmd && git restore . && git clean -dxf	
	-cd $(LIBDIR)/dtc && git restore . && git clean -dxf	

.PHONY: all clean libfdt libtar libmd libtar_patch
