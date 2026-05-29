PGFILEDESC = "opentenbase_ctl - Operation and maintenance tool for opentenbase instance"
PROGRAM = opentenbase_ctl

subdir = contrib/opentenbase_ctl
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk


override CPPFLAGS := $(CPPFLAGS) -Isrc -std=c++17 
override LDFLAGS :=  $(LDFLAGS)    -lpthread -lssh2 -lssl -lstdc++  

OBJS = src/main.o \
       src/ssh/remote_ssh.o \
       src/config/config.o \
       src/log/log.o \
       src/node/node.o \
       src/cluster/cluster.o \
       src/utils/utils.o \
       src/command/command.o \
       src/types/types.o \
       src/file/file.o

all: opentenbase_ctl

opentenbase_ctl:  $(OBJS) 
	$(CXX) $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) --std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -o $@$(X)
install: all installdirs
	$(INSTALL_PROGRAM) opentenbase_ctl$(X) '$(DESTDIR)$(bindir)'/opentenbase_ctl$(X)

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

check:
	$(prove_check)

installcheck:
	$(prove_installcheck)

uninstall:
	rm -f $(addprefix '$(DESTDIR)$(bindir)'/, opentenbase_ctl$(X))

TEST_OBJS = src/tests/test_opentenbase_ctl.o \
            src/ssh/remote_ssh.o \
            src/config/config.o \
            src/log/log.o \
            src/node/node.o \
            src/cluster/cluster.o \
            src/utils/utils.o \
            src/command/command.o \
            src/types/types.o \
            src/file/file.o

test: test_opentenbase_ctl
	./test_opentenbase_ctl

test_opentenbase_ctl: $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) --std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -o $@$(X)

# Standalone test build (no PostgreSQL build tree required)
test-standalone:
	g++ -std=c++17 -Isrc -o test_opentenbase_ctl \
	  src/tests/test_opentenbase_ctl.cpp \
	  src/ssh/remote_ssh.cpp \
	  src/config/config.cpp \
	  src/log/log.cpp \
	  src/node/node.cpp \
	  src/cluster/cluster.cpp \
	  src/utils/utils.cpp \
	  src/command/command.cpp \
	  src/types/types.cpp \
	  src/file/file.cpp \
	  -lpthread -lssh2 -lssl -lcrypto -lstdc++
	./test_opentenbase_ctl

clean distclean maintainer-clean:
	rm -f opentenbase_ctl$(X) test_opentenbase_ctl$(X)
	rm -f src/*.o
	rm -f src/**/*.o
