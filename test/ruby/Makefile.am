ACLOCAL_AMFLAGS = -I ../../m4 -Wnone

GEM_VERSION = @OML_ALT_VER@

if HAVE_CHECK
if BUILD_GEM
TESTS = check_gem.sh
endif
TESTS_ENVIRONMENT=TOPSRCDIR=$(top_srcdir) TOPBUILDDIR=$(top_builddir) GEM_VERSION=$(GEM_VERSION)
endif
EXTRA_DIST = check_gem.sh
