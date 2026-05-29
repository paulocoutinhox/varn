package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @ENGINESDIR @ENGINESDIR_REL_LIBDIR
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $VERSION @LDLIBS
);

our @PREFIX                     = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build' );
our @libdir                     = ( '' );
our @BINDIR                     = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/apps' );
our @BINDIR_REL_PREFIX          = ( 'apps' );
our @LIBDIR                     = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build' );
our @LIBDIR_REL_PREFIX          = ( '' );
our @INCLUDEDIR                 = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/include', '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/../../../../../../../.cache/CPM/openssl-source/f9ec/include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include', '../../../../../../../.cache/CPM/openssl-source/f9ec/include' );
our @APPLINKDIR                 = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/ms' );
our @APPLINKDIR_REL_PREFIX      = ( 'ms' );
our @ENGINESDIR                 = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/engines' );
our @ENGINESDIR_REL_LIBDIR      = ( 'engines' );
our @MODULESDIR                 = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build/providers' );
our @MODULESDIR_REL_LIBDIR      = ( 'providers' );
our @PKGCONFIGDIR               = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( '.' );
our @CMAKECONFIGDIR             = ( '/Users/paulo/Developer/workspaces/cpp/varn/build-verify/_deps/openssl-source-build' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( '.' );
our $VERSION                    = '3.6.1';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, ' ')
    : split(/ +/, ' ');

1;
