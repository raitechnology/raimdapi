# raimd makefile
lsb_dist     := $(shell if [ -f /etc/os-release ] ; then \
                  grep '^NAME=' /etc/os-release | sed 's/.*=[\"]*//' | sed 's/[ \"].*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -is ; else echo Linux ; fi)
lsb_dist_ver := $(shell if [ -f /etc/os-release ] ; then \
		  grep '^VERSION=' /etc/os-release | sed 's/.*=[\"]*//' | sed 's/[ \"].*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
uname_m      := $(shell uname -m)

short_dist_lc := $(patsubst CentOS,rh,$(patsubst RedHatEnterprise,rh,\
                   $(patsubst RedHat,rh,\
                     $(patsubst Fedora,fc,$(patsubst Ubuntu,ub,\
                       $(patsubst Debian,deb,$(patsubst SUSE,ss,$(lsb_dist))))))))
short_dist    := $(shell echo $(short_dist_lc) | tr a-z A-Z)
pwd           := $(shell pwd)
rpm_os        := $(short_dist_lc)$(lsb_dist_ver).$(uname_m)

# this is where the targets are compiled
build_dir ?= $(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)
bind        := $(build_dir)/bin
libd        := $(build_dir)/lib64
objd        := $(build_dir)/obj
dependd     := $(build_dir)/dep
java_classd := $(build_dir)/java
java_srcd   := ./src/raiapi/java

# --- language bindings ------------------------------------------------------
# java=0 / dotnet=0 / golang=0 disable a binding (rpm spec passes these from
# %bcond).
java   ?= 1
dotnet ?= 0
golang ?= 0

ifeq ($(java),1)
# Locate the JDK from whichever javac is active (follows alternatives / mock
# chroot paths like /usr/lib/jvm/java-21-openjdk-21.0.x-1.el9.x86_64).
JAVA_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v javac 2>/dev/null))))
java_home := $(JAVA_HOME)
ifeq ($(wildcard $(java_home)/include/jni.h),)
  $(error jni.h not found under '$(java_home)/include' -- install java-*-openjdk-devel, set JAVA_HOME, or build with java=0)
endif
jni_os       := $(shell uname -s | tr A-Z a-z)
jni_includes := -I$(java_home)/include -I$(java_home)/include/$(jni_os)
# Emit class files loadable by this JDK's runtime.  Spec sets jdk_release per
# chroot (11 on EL7, 21 elsewhere); default = whatever javac we found.
jdk_release ?= $(shell $(java_home)/bin/javac -version 2>&1 | sed -E 's/^javac ([0-9]+).*/\1/')
javac_flags := --release $(jdk_release)
JAVAC       := $(java_home)/bin/javac $(javac_flags)
endif

empty :=
space := $(empty) $(empty)

default_cflags := -ggdb -O3
# use 'make port_extra=-g' for debug build
ifeq (-g,$(findstring -g,$(port_extra)))
  default_cflags := -ggdb
endif
ifeq (-a,$(findstring -a,$(port_extra)))
  default_cflags += -fsanitize=address
endif
ifeq (-mingw,$(findstring -mingw,$(port_extra)))
  CC    := /usr/bin/x86_64-w64-mingw32-gcc
  CXX   := /usr/bin/x86_64-w64-mingw32-g++
  mingw := true
endif
ifeq (,$(port_extra))
  build_cflags := $(shell if [ -x /bin/rpm ]; then /bin/rpm --eval '%{optflags}' ; \
                          elif [ -x /bin/dpkg-buildflags ] ; then /bin/dpkg-buildflags --get CFLAGS ; fi)
endif
# msys2 using ucrt64
ifeq (MSYS2,$(lsb_dist))
  mingw := true
endif
CC          ?= gcc
CXX         ?= g++
cc          := $(CC) -std=c11
cpp         := $(CXX)
arch_cflags := -mavx -fno-omit-frame-pointer
gcc_wflags  := -Wall -Wextra
#-Werror
# if windows cross compile
ifeq (true,$(mingw))
dll       := dll
# mingw time.h provides localtime_r/gmtime_r behind this
default_cflags += -D_POSIX_THREAD_SAFE_FUNCTIONS
exe       := .exe
soflag    := -shared -Wl,--subsystem,windows
fpicflags := -fPIC -DAPI_SHARED
implib    := -Wl,--out-implib,
# raiapi is exception based, so unlike the lower layers it needs the C++
# runtime; link it statically so the dlls do not depend on libstdc++-6.dll
thread_lib  := -lwinpthread
sock_lib    := -lcares -lws2_32 -lpsapi -lwinmm
dynlink_lib := -lpcre2-8 -lz
cxxrt_lflags := -static-libstdc++ -static-libgcc
else
dll       := so
exe       :=
soflag    := -shared
fpicflags := -fPIC
thread_lib  := -pthread -lrt
sock_lib    := -lcares
dynlink_lib := -lpcre2-8 -lz
endif
# make apple shared lib
ifeq (Darwin,$(lsb_dist))
dll       := dylib
endif
# rpmbuild uses RPM_OPT_FLAGS
#ifeq ($(RPM_OPT_FLAGS),)
CFLAGS ?= $(build_cflags) $(default_cflags)
#else
#CFLAGS ?= $(RPM_OPT_FLAGS)
#endif
cflags := $(gcc_wflags) $(CFLAGS) $(arch_cflags)
lflags := $(cxxrt_lflags)

INCLUDES   ?= -Iinclude
DEFINES    ?=
includes   := $(INCLUDES)
defines    := $(DEFINES)

# if not linking libstdc++
ifdef NO_STL
cppflags    := -std=c++11 -fno-rtti -fno-exceptions
cpplink     := $(CC)
else
cppflags    := -std=c++11
cpplink     := $(CXX)
endif

.PHONY: everything
everything: all

math_lib := -lm

# test submodules exist (they don't exist for dist_rpm, dist_dpkg targets)
test_makefile = $(shell if [ -f ./$(1)/GNUmakefile ] ; then echo ./$(1) ; \
                        elif [ -f ../$(1)/GNUmakefile ] ; then echo ../$(1) ; fi)

md_home     := $(call test_makefile,raimd)
dec_home    := $(call test_makefile,libdecnumber)
kv_home     := $(call test_makefile,raikv)
sassrv_home := $(call test_makefile,sassrv)
omm_home    := $(call test_makefile,omm)

ifeq (,$(dec_home))
dec_home    := $(call test_makefile,$(md_home)/libdecnumber)
endif

lnk_lib     := -Wl,--push-state -Wl,-Bstatic
dlnk_lib    :=
lnk_dep     :=
dlnk_dep    :=

# omm first: it depends on sassrv/raimd/raikv below
ifneq (,$(omm_home))
omm_lib     := $(omm_home)/$(libd)/libomm.a
omm_dll     := $(omm_home)/$(libd)/libomm.$(dll)
ommapi_lib  := $(omm_home)/$(libd)/libommapi.a
ommapi_dll  := $(omm_home)/$(libd)/libommapi.$(dll)
lnk_lib     += $(ommapi_lib) $(omm_lib)
lnk_dep     += $(ommapi_lib) $(omm_lib)
dlnk_lib    += -L$(omm_home)/$(libd) -lommapi -lomm
dlnk_dep    += $(ommapi_dll) $(omm_dll)
rpath5       = ,-rpath,$(pwd)/$(omm_home)/$(libd)
includes    += -I$(omm_home)/include
else
lnk_lib     += $(push_static) -lommapi -lomm $(pop_static)
dlnk_lib    += -lommapi -lomm
endif

ifneq (,$(sassrv_home))
sassrv_lib  := $(sassrv_home)/$(libd)/libsassrv.a
sassrv_dll  := $(sassrv_home)/$(libd)/libsassrv.$(dll)
rv7lib_lib  := $(sassrv_home)/$(libd)/librv7lib.a
rv7lib_dll  := $(sassrv_home)/$(libd)/librv7lib.$(dll)
lnk_lib     += $(rv7lib_lib) $(sassrv_lib)
lnk_dep     += $(rv7lib_lib) $(sassrv_lib)
dlnk_lib    += -L$(sassrv_home)/$(libd) -lrv7lib -lsassrv
dlnk_dep    += $(rv7lib_dll) $(sassrv_dll)
rpath4       = ,-rpath,$(pwd)/$(sassrv_home)/$(libd)
includes    += -I$(sassrv_home)/include
else
lnk_lib     += $(push_static) -lrv7lib -lsassrv $(pop_static)
dlnk_lib    += -lrv7lib -lsassrv
endif

ifneq (,$(md_home))
md_lib      := $(md_home)/$(libd)/libraimd.a
md_dll      := $(md_home)/$(libd)/libraimd.$(dll)
lnk_lib     += $(md_lib)
lnk_dep     += $(md_lib)
dlnk_lib    += -L$(md_home)/$(libd) -lraimd
dlnk_dep    += $(md_dll)
rpath1       = ,-rpath,$(pwd)/$(md_home)/$(libd)
includes    += -I$(md_home)/include
else
lnk_lib     += $(push_static) -lraimd $(pop_static)
dlnk_lib    += -lraimd
endif

ifneq (,$(dec_home))
dec_lib     := $(dec_home)/$(libd)/libdecnumber.a
dec_dll     := $(dec_home)/$(libd)/libdecnumber.$(dll)
lnk_lib     += $(dec_lib)
lnk_dep     += $(dec_lib)
dlnk_lib    += -L$(dec_home)/$(libd) -ldecnumber
dlnk_dep    += $(dec_dll)
rpath2       = ,-rpath,$(pwd)/$(dec_home)/$(libd)
dec_includes = -I$(dec_home)/include
else
lnk_lib     += $(push_static) -ldecnumber $(pop_static)
dlnk_lib    += -ldecnumber
endif

ifneq (,$(kv_home))
kv_lib      := $(kv_home)/$(libd)/libraikv.a
kv_dll      := $(kv_home)/$(libd)/libraikv.$(dll)
lnk_lib     += $(kv_lib)
lnk_dep     += $(kv_lib)
dlnk_lib    += -L$(kv_home)/$(libd) -lraikv
dlnk_dep    += $(kv_dll)
rpath3       = ,-rpath,$(pwd)/$(kv_home)/$(libd)
includes    += -I$(kv_home)/include
else
lnk_lib     += $(push_static) -lraikv $(pop_static)
dlnk_lib    += -lraikv
endif

rpath   := -Wl,-rpath,$(pwd)/$(libd)$(rpath1)$(rpath2)$(rpath3)$(rpath4)$(rpath5)
lnk_lib += -Wl,--pop-state

.PHONY: everything
everything: $(kv_lib) $(dec_lib) $(md_lib) $(sassrv_lib) $(omm_lib) all

clean_subs :=
# build submodules if have them
ifneq (,$(kv_home))
$(kv_lib) $(kv_dll):
	$(MAKE) -C $(kv_home)
.PHONY: clean_kv
clean_kv:
	$(MAKE) -C $(kv_home) clean
clean_subs += clean_kv
endif
ifneq (,$(dec_home))
$(dec_lib) $(dec_dll):
	$(MAKE) -C $(dec_home)
.PHONY: clean_dec
clean_dec:
	$(MAKE) -C $(dec_home) clean
clean_subs += clean_dec
endif
ifneq (,$(md_home))
$(md_lib) $(md_dll):
	$(MAKE) -C $(md_home)
.PHONY: clean_md
clean_md:
	$(MAKE) -C $(md_home) clean
clean_subs += clean_md
endif
ifneq (,$(sassrv_home))
$(sassrv_lib) $(sassrv_dll):
	$(MAKE) -C $(sassrv_home)
.PHONY: clean_sassrv
clean_sassrv:
	$(MAKE) -C $(sassrv_home) clean
clean_subs += clean_sassrv
endif
ifneq (,$(omm_home))
$(omm_lib) $(omm_dll):
	$(MAKE) -C $(omm_home)
.PHONY: clean_omm
clean_omm:
	$(MAKE) -C $(omm_home) clean
clean_subs += clean_omm
endif

# copr/fedora build (with version env vars)
# copr uses this to generate a source rpm with the srpm target
-include .copr/Makefile
# debian build (debuild)
# target for building installable deb: dist_dpkg
-include deb/Makefile

all_exes    :=
all_libs    :=
all_dlls    :=
all_depends :=

# --- libraimdapi: one library -------------------------------------------------
# v1 api (raiapi.h, namespace rai_old), v2 api (raiapi2.h), the C binding
# (raiapi2_c.h), the message library (msg/*.h) and base/stream/util.  The v1
# classes are in rai_old so that the same names (RaiApi, RaiSession, ...) can
# coexist with v2 in one object.  Named raimdapi (not raiapi/raiapi2, which
# are RaiCore's libraries) to keep the two code bases apart.
raiapi_files    := raiapi
raiapi2_files   := raiapi2 raiapi2_tibrv raiapi2_omm
raiapi2c_files  := raiapi2_c
base_files      := sys time log file thread mem dir
stream_files    := io_stream file_stream stdio_stream byte_array_stream cycle_stream
util_files      := snprintf strptime hash_util str_util args
msg_files       := cfile_parser msg field dict subject sass_const wildcard mfeed_dict rai_form_msg
libraimdapi_files := $(raiapi_files) $(raiapi2_files) $(raiapi2c_files) $(msg_files) \
                   $(base_files) $(stream_files) $(util_files)
libraimdapi_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(libraimdapi_files)))
libraimdapi_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libraimdapi_files)))
libraimdapi_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(libraimdapi_files))) \
                   $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libraimdapi_files)))
libraimdapi_dlnk  := $(dlnk_lib)
libraimdapi_spec  := $(ver_build)_$(git_hash)
libraimdapi_ver   := $(major_num).$(minor_num)

$(libd)/libraimdapi.a: $(libraimdapi_objs)
$(libd)/libraimdapi.$(dll): $(libraimdapi_dbjs) $(lnk_dep) $(dlnk_dep)

all_libs    += $(libd)/libraimdapi.a
all_dlls    += $(libd)/libraimdapi.$(dll)
all_depends += $(libraimdapi_deps)

raiapi_lib  := $(libd)/libraimdapi.a
raiapi_lnk  := $(libd)/libraimdapi.a $(lnk_lib)
raiapi_dlib := $(libd)/libraimdapi.$(dll)
ifeq (true,$(mingw))
# On windows raimdapi.dll exports only the C api (raiapi2_c.h uses dllexport,
# which turns off the automatic export of everything else): that is what
# .NET / C callers need, and the C++ classes would be mingw-ABI only anyway.
# The exes and the JNI dlls link the C++ api from the static library, and
# the sibling libs from lnk_lib, so they are self contained.
raiapi_dlnk := $(libd)/libraimdapi.a $(lnk_lib)
# ... so the exe / JNI-dll prerequisites must be the static libs they link,
# not the dll: otherwise a changed .a never relinks them (and with -j the
# exe can link before the .a is rebuilt)
raiapi_dlib := $(libd)/libraimdapi.a $(lnk_dep)
else
raiapi_dlnk := -lraimdapi
endif

raisub_files := raisub raisampleutil
raisub_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raisub_files)))
raisub_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raisub_files)))
raisub_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raisub_files)))
raisub_libs  := $(raiapi_dlib)
raisub_lnk   := $(raiapi_dlnk)

$(bind)/raisub$(exe): $(raisub_objs) $(raisub_libs)
all_exes += $(bind)/raisub$(exe)
all_depends +=  $(raisub_deps)

raipub_files := raipub raisampleutil
raipub_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raipub_files)))
raipub_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raipub_files)))
raipub_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raipub_files)))
raipub_libs  := $(raiapi_dlib)
raipub_lnk   := $(raiapi_dlnk)

$(bind)/raipub$(exe): $(raipub_objs) $(raipub_libs)
all_exes += $(bind)/raipub$(exe)
all_depends +=  $(raipub_deps)

raiping_files := raiping raisampleutil
raiping_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raiping_files)))
raiping_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raiping_files)))
raiping_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raiping_files)))
raiping_libs  := $(raiapi_dlib)
raiping_lnk   := $(raiapi_dlnk)

$(bind)/raiping$(exe): $(raiping_objs) $(raiping_libs)
all_exes += $(bind)/raiping$(exe)
all_depends +=  $(raiping_deps)

raiapi2_lib  := $(raiapi_lib)
raiapi2_lnk  := $(raiapi_lnk)
raiapi2_dlib := $(raiapi_dlib)
raiapi2_dlnk := $(raiapi_dlnk)

raisub2_files := raisub2
raisub2_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raisub2_files)))
raisub2_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raisub2_files)))
raisub2_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raisub2_files)))
raisub2_libs  := $(raiapi2_dlib)
raisub2_lnk   := $(raiapi2_dlnk)

$(bind)/raisub2$(exe): $(raisub2_objs) $(raisub2_libs)
all_exes += $(bind)/raisub2$(exe)
all_depends +=  $(raisub2_deps)

raipub2_files := raipub2
raipub2_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raipub2_files)))
raipub2_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raipub2_files)))
raipub2_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raipub2_files)))
raipub2_libs  := $(raiapi2_dlib)
raipub2_lnk   := $(raiapi2_dlnk)

$(bind)/raipub2$(exe): $(raipub2_objs) $(raipub2_libs)
all_exes += $(bind)/raipub2$(exe)
all_depends +=  $(raipub2_deps)

raireplay2_files := raireplay2
raireplay2_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raireplay2_files)))
raireplay2_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raireplay2_files)))
raireplay2_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raireplay2_files)))
raireplay2_libs  := $(raiapi2_dlib)
raireplay2_lnk   := $(raiapi2_dlnk)

$(bind)/raireplay2$(exe): $(raireplay2_objs) $(raireplay2_libs)
all_exes += $(bind)/raireplay2$(exe)
all_depends +=  $(raireplay2_deps)

raiping2_files := raiping2
raiping2_cfile := $(addprefix src/raiapi/c++/, $(addsuffix .cpp, $(raiping2_files)))
raiping2_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(raiping2_files)))
raiping2_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(raiping2_files)))
raiping2_libs  := $(raiapi2_dlib)
raiping2_lnk   := $(raiapi2_dlnk)

$(bind)/raiping2$(exe): $(raiping2_objs) $(raiping2_libs)
all_exes += $(bind)/raiping2$(exe)
all_depends +=  $(raiping2_deps)

rai_api_jni_includes = $(jni_includes) -Isrc
libjraiapi2_files := rai_api_jni
libjraiapi2_objs  := $(addprefix src/raiapi/c++/java/com/rai/raiapi2/, $(addsuffix .cpp, $(libjraiapi2_files)))
libjraiapi2_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libjraiapi2_files)))
libjraiapi2_deps  := $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libjraiapi2_files)))
libjraiapi2_dlnk  := -L$(libd) $(raiapi2_dlnk) $(dlnk_lib)
libjraiapi2_spec  := $(ver_build)_$(git_hash)
libjraiapi2_ver   := $(major_num).$(minor_num)

$(libd)/libjraiapi2.$(dll): $(libjraiapi2_dbjs) $(raiapi2_dlib) $(lnk_dep) $(dlnk_dep)

rai_msg_jni_includes = $(jni_includes) -Isrc
libjraimsg_files := rai_msg_jni
libjraimsg_objs  := $(addprefix src/raiapi/c++/java/com/rai/raiapi2/, $(addsuffix .cpp, $(libjraimsg_files)))
libjraimsg_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libjraimsg_files)))
libjraimsg_deps  := $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libjraimsg_files)))
libjraimsg_dlnk  := -L$(libd) $(raiapi2_dlnk) $(dlnk_lib)
libjraimsg_spec  := $(ver_build)_$(git_hash)
libjraimsg_ver   := $(major_num).$(minor_num)

$(libd)/libjraimsg.$(dll): $(libjraimsg_dbjs) $(raiapi2_dlib) $(lnk_dep) $(dlnk_dep)

ifeq ($(java),1)
all_dlls    += $(libd)/libjraiapi2.$(dll) $(libd)/libjraimsg.$(dll)
all_depends += $(libjraiapi2_deps) $(libjraimsg_deps)
endif


raiexception_root     = com/rai/raiexception
raiexception_classes  = $(java_classd)/$(raiexception_root)/RaiException.class
raiexception_manifest = $(java_classd)/raiexception_manifest.txt
$(libd)/raiexception.jar: $(raiexception_classes) $(raiexception_manifest)

ifeq ($(java),1)
all_libs += $(libd)/raiexception.jar
endif

raimsg_root     = com/rai/raimsg
raimsg_classes  = $(java_classd)/$(raimsg_root)/RaiMsg.class $(java_classd)/$(raimsg_root)/RaiField.class \
                  $(java_classd)/$(raimsg_root)/Partial.class $(java_classd)/$(raimsg_root)/SassConst.class \
		  $(java_classd)/$(raimsg_root)/RaiMsgException.class
raimsg_manifest = $(java_classd)/raimsg_manifest.txt
$(libd)/raimsg.jar: $(raimsg_classes) $(raimsg_manifest)

ifeq ($(java),1)
all_libs += $(libd)/raimsg.jar
endif

raiapi_root    = com/rai/raiapi
raiapi_classes = $(java_classd)/$(raiapi_root)/RaiApi.class $(java_classd)/$(raiapi_root)/RaiCallback.class \
                 $(java_classd)/$(raiapi_root)/RaiDict.class $(java_classd)/$(raiapi_root)/RaiEvent.class \
                 $(java_classd)/$(raiapi_root)/RaiException.class $(java_classd)/$(raiapi_root)/RaiPublish.class \
                 $(java_classd)/$(raiapi_root)/RaiSession.class $(java_classd)/$(raiapi_root)/RaiSubscribe.class \
                 $(java_classd)/$(raiapi_root)/RaiSession.class $(java_classd)/$(raiapi_root)/RaiTimerCallback.class \
                 $(java_classd)/$(raiapi_root)/RaiTimer.class $(java_classd)/$(raiapi_root)/SubHandle.class \
                 $(java_classd)/$(raiapi_root)/TimerHandle.class
raiapi_manifest = $(java_classd)/raiapi_manifest.txt
$(libd)/raiapi.jar: $(raiapi_classes) $(raiapi_manifest)

ifeq ($(java),1)
all_libs += $(libd)/raiapi.jar
endif

raiapi2_root    = com/rai/raiapi2
raiapi2_classes = $(java_classd)/$(raiapi2_root)/Args.class $(java_classd)/$(raiapi2_root)/RaiMsgEvent.class \
                  $(java_classd)/$(raiapi2_root)/BoolArg.class $(java_classd)/$(raiapi2_root)/RaiPublish.class \
                  $(java_classd)/$(raiapi2_root)/DoubleArg.class $(java_classd)/$(raiapi2_root)/RaiQueue.class \
                  $(java_classd)/$(raiapi2_root)/IntArg.class $(java_classd)/$(raiapi2_root)/RaiApiException.class \
                  $(java_classd)/$(raiapi2_root)/RaiApi.class $(java_classd)/$(raiapi2_root)/RaiSession.class \
                  $(java_classd)/$(raiapi2_root)/RaiSubscribeCallback.class $(java_classd)/$(raiapi2_root)/RaiSubscribeEvent.class \
                  $(java_classd)/$(raiapi2_root)/RaiConnectionEvent.class $(java_classd)/$(raiapi2_root)/RaiSubscribe.class \
                  $(java_classd)/$(raiapi2_root)/RaiDataLossCallback.class $(java_classd)/$(raiapi2_root)/RaiTimerCallback.class \
                  $(java_classd)/$(raiapi2_root)/RaiDataLossEvent.class $(java_classd)/$(raiapi2_root)/RaiTimer.class \
                  $(java_classd)/$(raiapi2_root)/RaiDict.class $(java_classd)/$(raiapi2_root)/StringArg.class \
                  $(java_classd)/$(raiapi2_root)/RaiEntitlement.class $(java_classd)/$(raiapi2_root)/Time.class \
                  $(java_classd)/$(raiapi2_root)/RaiInteractivePublish.class $(java_classd)/$(raiapi2_root)/TimeRotate.class \
                  $(java_classd)/$(raiapi2_root)/RaiMsgCallback.class
raiapi2_manifest = $(java_classd)/raiapi2_manifest.txt
$(libd)/raiapi2.jar: $(raiapi2_classes) $(raiapi2_manifest)

ifeq ($(java),1)
all_libs += $(libd)/raiapi2.jar
endif

# --- java programs ----------------------------------------------------------
# Each program <p> compiles into its own staging dir $(java_classd)/<p>/ and is
# packaged as $(libd)/<p>.jar, so helper/nested classes (Args, $SubThread...)
# stay out of lib64.  Launcher $(bind)/j<p> puts lib64 + every lib64 jar on the
# classpath and names the main class.  Per program, declare only its jar deps:
#     <p>_jars := ...
# then add $(bind)/j<p> to java_progs.
raisub_jars     := $(libd)/raiapi.jar  $(libd)/raimsg.jar $(libd)/raiexception.jar
raisub2_jars    := $(libd)/raiapi2.jar $(libd)/raimsg.jar $(libd)/raiexception.jar
raiping_jars    := $(libd)/raiapi.jar  $(libd)/raimsg.jar $(libd)/raiexception.jar
raiping2_jars   := $(libd)/raiapi2.jar $(libd)/raimsg.jar $(libd)/raiexception.jar
raipub_jars     := $(libd)/raiapi.jar  $(libd)/raimsg.jar $(libd)/raiexception.jar
raipub2_jars    := $(libd)/raiapi2.jar $(libd)/raimsg.jar $(libd)/raiexception.jar
raireplay2_jars := $(libd)/raiapi2.jar $(libd)/raimsg.jar $(libd)/raiexception.jar

java_progs := $(bind)/jraisub $(bind)/jraisub2 \
              $(bind)/jraiping $(bind)/jraiping2 \
              $(bind)/jraipub $(bind)/jraipub2 \
                              $(bind)/jraireplay2
java_prog_names := $(patsubst $(bind)/j%,%,$(java_progs))
java_prog_jars  := $(addprefix $(libd)/, $(addsuffix .jar, $(java_prog_names)))

# stamp file = "all classes for <p> compiled into $(java_classd)/<p>/"
.SECONDEXPANSION:
$(java_classd)/%.stamp: $(java_srcd)/%.java $$($$*_jars)
	rm -rf $(java_classd)/$* && mkdir -p $(java_classd)/$*
	$(JAVAC) -classpath $(subst $(space),:,$($*_jars)) -d $(java_classd)/$* $<
	@touch $@

# program jar: everything the compile produced, whatever the class names.
# Static pattern rule so it applies ONLY to program jars, never the API jars
# built by the generic $(libd)/%.jar rule below.
$(java_prog_jars): $(libd)/%.jar: $(java_classd)/%.stamp
	jar cf $@ -C $(java_classd)/$* .

$(bind)/j%: $(libd)/%.jar GNUmakefile
	@{ echo '#!/bin/sh'; \
	   echo '# generated by GNUmakefile - do not edit'; \
	   echo 'here=$$(cd "$$(dirname "$$0")/.." && pwd)'; \
	   echo 'lib="$$here/lib64"'; \
	   echo 'cp="$$lib"; for j in "$$lib"/*.jar; do cp="$$cp:$$j"; done'; \
	   echo '# JNI libs (libjraimsg.so, libjraiapi2.so) live beside the jars'; \
	   echo 'exec java -Djava.library.path="$$lib" -classpath "$$cp" $* "$$@"'; } > $@
	chmod +x $@

ifeq ($(java),1)
all_exes += $(java_progs)
endif

# --- .NET binding (dotnet=1) ------------------------------------------------
# src/raiapi/dotnet: RaiApi2 (netstandard2.0 class library over libraimdapi)
# and the raisub2/raipub2/raiping2/raireplay2 programs (net9.0).  msbuild
# writes to $(dotnet_outd), the assemblies are copied to $(libd)/dotnet and a
# launcher script d<prog> is generated in $(bind), like the java j<prog>.
dotnet_srcd  := src/raiapi/dotnet
dotnet_outd  := $(build_dir)/dotnet
dotnet_libd  := $(libd)/dotnet
dotnet_progs_names := raisub2 raipub2 raiping2 raireplay2
dotnet_src   := $(wildcard $(dotnet_srcd)/*/*.cs $(dotnet_srcd)/*/*.csproj $(dotnet_srcd)/*.sln $(dotnet_srcd)/*.props)
dotnet_stamp := $(dotnet_outd)/build.stamp
dotnet_progs := $(addprefix $(bind)/d, $(dotnet_progs_names))

$(dotnet_stamp): $(dotnet_src) $(libd)/libraimdapi.$(dll) $(version_h)
	dotnet build $(dotnet_srcd)/raiapi2.sln -c Release -nologo -v q -p:RaiBuildDir=$(abspath $(dotnet_outd)) \
	  -p:Version=$(version) -p:FileVersion=$(version).$(build_num) -p:InformationalVersion="$(version_str)"
	mkdir -p $(dotnet_libd)
	cp -f $(dotnet_outd)/bin/RaiApi2/Release/netstandard2.0/RaiApi2.dll $(dotnet_libd)/
	for p in $(dotnet_progs_names) ; do \
	  cp -f $(dotnet_outd)/bin/$$p/Release/*/$$p.dll $(dotnet_outd)/bin/$$p/Release/*/$$p.runtimeconfig.json \
	        $(dotnet_outd)/bin/$$p/Release/*/$$p.deps.json $(dotnet_libd)/ ; \
	done
	touch $@

$(bind)/d%: $(dotnet_stamp) GNUmakefile
	@{ echo '#!/bin/sh'; \
	   echo '# generated by GNUmakefile - do not edit'; \
	   echo 'here=$$(cd "$$(dirname "$$0")/.." && pwd)'; \
	   echo 'lib="$$here/lib64"'; \
	   echo '# libraimdapi.so (P/Invoke) and its deps live in lib64'; \
	   echo 'LD_LIBRARY_PATH="$$lib$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}"; export LD_LIBRARY_PATH'; \
	   echo 'exec dotnet "$$lib/dotnet/$*.dll" "$$@"'; } > $@
	chmod +x $@

ifeq ($(dotnet),1)
all_libs += $(dotnet_stamp)
all_exes += $(dotnet_progs)
endif

# --- Go binding (golang=1) ---------------------------------------------------
# src/raiapi/golang: package raiapi2 (cgo over libraimdapi, include/raiapi2_c.h)
# and the raisub2/raipub2/raiping2/raireplay2 programs, installed as g<prog>
# in $(bind) like the java j<prog> / .NET d<prog>.  The binaries are ELF with
# an rpath to $(libd) (removed by dist_bins, like the C++ programs); the
# binding version is stamped from the same variables as the C++/java/.NET.
golang_srcd  := src/raiapi/golang
golang_mod   := github.com/injinj/raimdapi/src/raiapi/golang
golang_progs_names := raisub2 raipub2 raiping2 raireplay2
golang_src   := $(wildcard $(golang_srcd)/go.mod $(golang_srcd)/raiapi2/*.go \
                           $(golang_srcd)/raiapi2/*.c $(golang_srcd)/cmd/*/*.go)
golang_progs := $(addprefix $(bind)/g, $(golang_progs_names))
golang_env   := CGO_ENABLED=1 CGO_CFLAGS="-I$(abspath include)" \
                CGO_LDFLAGS="-L$(abspath $(libd)) -Wl,-rpath,$(abspath $(libd))"
# recursive (=): version_str is defined further down
golang_flags  = -ldflags "-X '$(golang_mod)/raiapi2.bindingVersion=$(version_str)'"

$(bind)/g%: $(golang_src) $(libd)/libraimdapi.$(dll) $(version_h)
	@mkdir -p $(bind)
	cd $(golang_srcd) && $(golang_env) go build $(golang_flags) -o $(abspath $@) ./cmd/$*

.PHONY: golang_vet
golang_vet:
	cd $(golang_srcd) && $(golang_env) go vet ./...

ifeq ($(golang),1)
all_exes += $(golang_progs)
endif

all_dirs := $(bind) $(libd) $(objd) $(dependd) $(java_classd)

# --- version -----------------------------------------------------------------
# The package version comes from .copr/Makefile (major/minor/patch/build_num)
# plus the git hash.  It is written to a generated header so that
# RaiApi::RaiVersion() (C++, and through it the C api, java and .NET) reports
# the same string as the rpm; the file is only rewritten when the content
# changes, so nothing rebuilds needlessly.  The java manifests and the .NET
# assembly version are stamped from the same variables (see below).
version_h   := $(objd)/raimdapi_version.h
version_str := $(ver_build) ($(git_hash))
.PHONY: version_check
version_check:
	@mkdir -p $(objd)
	@{ echo '/* generated by GNUmakefile - do not edit */'; \
	   echo '#define RAIMDAPI_VERSION "$(version)"'; \
	   echo '#define RAIMDAPI_BUILD   "$(ver_build)"'; \
	   echo '#define RAIMDAPI_GIT     "$(git_hash)"'; \
	   echo '#define RAIMDAPI_VER_STR "$(version_str)"'; } > $(version_h).tmp
	@if cmp -s $(version_h).tmp $(version_h) ; then rm -f $(version_h).tmp ; \
	 else mv -f $(version_h).tmp $(version_h) ; fi
$(version_h): version_check
# the objects that bake the version in
$(objd)/raiapi.o $(objd)/raiapi.fpic.o $(objd)/raiapi2.o $(objd)/raiapi2.fpic.o: $(version_h)
raiapi_includes  = -I$(objd)
raiapi2_includes = -I$(objd)

all: $(all_libs) $(all_dlls) $(all_exes)

$(libd)/%.jar:
	jar cvfm $@ $($(*)_manifest) $(addprefix -C $(java_classd) $($(*)_root)/, $(notdir $($(*)_classes)))

# one javac per class, so with -j several run at once: -implicit:none stops
# each from also writing the classes of the sources it pulls in, and
# -Xprefer:source stops it from reading a sibling's half-written .class;
# the package ordering below keeps a package's dependencies complete first.
$(java_classd)/%.class: $(java_srcd)/%.java
	$(JAVAC) -implicit:none -Xprefer:source -sourcepath $(java_srcd) -classpath $(java_classd) $< -d $(java_classd)

$(raimsg_classes):  $(raiexception_classes)
$(raiapi2_classes): $(raimsg_classes) $(raiexception_classes)
$(raiapi_classes):  $(raiapi2_classes)

$(java_classd)/%_manifest.txt: $(version_h)
	echo "Implementation-Title:" $(notdir $*) > $@
	echo "Implementation-Version:" $(ver_build) >> $@
	echo "Implementation-Vendor: http://www.raitechnology.com/" >> $@
	echo "Rai-Git-Hash:" $(git_hash) >> $@

# create directories
$(dependd):
	@mkdir -p $(all_dirs)

# remove target bins, objs, depends
.PHONY: clean
clean:
	rm -r -f $(bind) $(libd) $(objd) $(dependd)
	if [ "$(build_dir)" != "." ] ; then rmdir $(build_dir) ; fi

.PHONY: clean_dist
clean_dist:
	rm -rf dpkgbuild rpmbuild

.PHONY: clean_all
clean_all: clean clean_dist

# the version header must exist before the .d files are generated
$(all_depends): | $(version_h)
$(dependd)/depend.make: $(dependd) $(all_depends)
	@echo "# depend file" > $(dependd)/depend.make
	@cat $(all_depends) >> $(dependd)/depend.make

ifeq (SunOS,$(lsb_dist))
remove_rpath = rpath -r
else
remove_rpath = chrpath -d
endif

# target used by rpmbuild, dpkgbuild
.PHONY: dist_bins
# the j<prog> / d<prog> launchers are shell scripts, only the ELF files have
# an rpath to remove
rpath_bins := $(all_dlls) $(filter-out $(java_progs) $(dotnet_progs), $(all_exes))
dist_bins: $(all_libs) $(all_dlls) $(all_exes) $(if $(filter 1,$(java)),$(java_progs)) $(if $(filter 1,$(dotnet)),$(dotnet_progs))
	for p in $(rpath_bins) ; do $(remove_rpath) $$p ; done

# --- windows distribution (make port_extra=-mingw [dotnet=1] dist_win) ------
# Everything a Windows box needs, in one directory: the exes and dlls are
# statically linked against the sibling libraries, so only the four mingw
# runtime dlls come along.  Windows loaders look for names without the "lib"
# prefix (.NET DllImport("raimdapi") -> raimdapi.dll, java loadLibrary
# ("jraiapi2") -> jraiapi2.dll), so the dlls are renamed on the way in.  The
# .NET programs are published as win-x64 apphosts named d<prog>.exe (like the
# d<prog> launchers on linux) so they do not collide with the C++ <prog>.exe.
# Produces <build_dir>/dist/raimdapi-<ver>-win64.zip and, when makensis
# (mingw32-nsis) is installed, the raimdapi-<ver>-win64-setup.exe installer.
mingw_bin   := /usr/x86_64-w64-mingw32/sys-root/mingw/bin
mingw_rtdll := libcares-2.dll libpcre2-8-0.dll libwinpthread-1.dll zlib1.dll
dist_name   := $(name)-$(ver_build)-win64
distd       := $(build_dir)/dist/$(dist_name)
dist_strip  := x86_64-w64-mingw32-strip
dotnet_win  := $(build_dir)/dotnet-win

.PHONY: dist_win
dist_win: all
	rm -rf $(distd) && mkdir -p $(distd)/bin $(distd)/lib $(distd)/include
	for f in $(all_exes) ; do case $$f in *.exe) $(dist_strip) -o $(distd)/bin/$$(basename $$f) $$f ;; esac ; done
	$(dist_strip) -o $(distd)/bin/raimdapi.dll $(libd)/libraimdapi.$(dll)
	cp -f $(libd)/libraimdapi.$(dll).a $(distd)/lib/raimdapi.lib
	cp -f $(libd)/libraimdapi.a $(distd)/lib/libraimdapi.a
	for d in $(mingw_rtdll) ; do cp -f $(mingw_bin)/$$d $(distd)/bin/ ; done
	cp -a include/*.h include/base include/msg include/stream include/util $(distd)/include/
	cp -f win/README-windows.md $(distd)/README.md
ifeq ($(java),1)
	$(dist_strip) -o $(distd)/bin/jraiapi2.dll $(libd)/libjraiapi2.$(dll)
	$(dist_strip) -o $(distd)/bin/jraimsg.dll $(libd)/libjraimsg.$(dll)
	cp -f $(libd)/*.jar $(distd)/lib/
	for p in $(java_progs) ; do n=$$(basename $$p) ; n=$${n#j} ; \
	  { printf '@echo off\r\n'; \
	    printf 'rem generated by GNUmakefile - do not edit\r\n'; \
	    printf 'set here=%%~dp0\r\n'; \
	    printf 'java -Djava.library.path="%%here%%." -classpath "%%here%%..\\lib\\*" %s %%*\r\n' $$n ; } > $(distd)/bin/j$$n.cmd ; done
endif
ifeq ($(dotnet),1)
	rm -rf $(dotnet_win)
	for p in $(dotnet_progs_names) ; do \
	  dotnet publish $(dotnet_srcd)/$$p/$$p.csproj -c Release -r win-x64 --no-self-contained -nologo -v q \
	    -p:RaiBuildDir=$(abspath $(dotnet_win)) -o $(distd)/bin \
	    -p:Version=$(version) -p:FileVersion=$(version).$(build_num) -p:InformationalVersion="$(version_str)" ; done
	rm -f $(distd)/bin/*.pdb
endif
	cd $(build_dir)/dist && rm -f $(dist_name).zip && zip -qr $(dist_name).zip $(dist_name)
	@if command -v makensis >/dev/null 2>&1 ; then \
	  makensis -V2 -DNAME=$(name) -DVERSION=$(version) -DVER_BUILD=$(ver_build) -DSRC=$(abspath $(distd)) \
	           -DOUT=$(abspath $(build_dir)/dist/$(dist_name)-setup.exe) win/raimdapi.nsi ; \
	else echo "makensis not found (dnf install mingw32-nsis), zip only" ; fi
	@ls -la $(build_dir)/dist/*.zip $(build_dir)/dist/*.exe 2>/dev/null

# target for building installable rpm
.PHONY: dist_rpm
dist_rpm: srpm
	( cd rpmbuild && rpmbuild --define "-topdir `pwd`" -ba SPECS/raimdapi.spec )

# force a remake of depend using 'make -B depend'
.PHONY: depend
depend: $(dependd)/depend.make

# dependencies made by 'make depend'
-include $(dependd)/depend.make

ifeq ($(DESTDIR),)
# 'sudo make install' puts things in /usr/local/lib, /usr/local/include
install_prefix ?= /usr/local
else
# debuild uses DESTDIR to put things into debian/libdecnumber/usr
install_prefix = $(DESTDIR)/usr
endif
# this should be 64 for rpm based, /64 for SunOS
install_lib_suffix ?=

install: dist_bins
	install -d $(install_prefix)/lib$(install_lib_suffix)
	install -d $(install_prefix)/bin $(install_prefix)/include/raiapi
	for f in $(libd)/libraimdapi.* ; do \
	if [ -h $$f ] ; then \
	cp -a $$f $(install_prefix)/lib$(install_lib_suffix) ; \
	else \
	install $$f $(install_prefix)/lib$(install_lib_suffix) ; \
	fi ; \
	done
	install -m 644 include/raiapi/*.h $(install_prefix)/include/raiapi

$(objd)/%.o: src/raiapi/c++/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/raiapi/c/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/base/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/util/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/stream/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/msg/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/util/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/raiapi/java/com/rai/raiapi2/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/raiapi/c/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/raiapi/java/com/rai/raimsg/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/raiapi/c++/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/base/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/util/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/stream/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/msg/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/util/%.c
	$(cc) $(cflags) $(fpicflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(libd)/%.a:
	ar rc $@ $($(*)_objs)

ifeq (Darwin,$(lsb_dist))
$(libd)/%.dylib:
	$(cpplink) -dynamiclib $(cflags) $(lflags) -o $@.$($(*)_dylib).dylib -current_version $($(*)_dylib) -compatibility_version $($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_dylib).dylib $(@F).$($(*)_ver).dylib && ln -f -s $(@F).$($(*)_ver).dylib $(@F)
else
$(libd)/%.$(dll):
	$(cpplink) $(soflag) $(rpath) $(cflags) $(lflags) -o $@.$($(*)_spec) -Wl,-soname=$(@F).$($(*)_ver) $(if $(implib),$(implib)$@.a) $($(*)_dbjs) $($(*)_dlnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_spec) $(@F).$($(*)_ver) && ln -f -s $(@F).$($(*)_ver) $(@F)
endif

$(bind)/%$(exe):
	$(cpplink) $(cflags) $(lflags) $(rpath) -o $@ $($(*)_objs) -L$(libd) $($(*)_lnk) $(cpp_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(dependd)/%.d: src/raiapi/c++/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/base/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/util/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/stream/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/msg/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/util/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/raiapi/c/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: src/raiapi/java/com/rai/raiapi2/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/raiapi/java/com/rai/raimsg/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/raiapi/c/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/raiapi/c++/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/base/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/util/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/stream/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/msg/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/util/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

