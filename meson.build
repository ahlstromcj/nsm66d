#*****************************************************************************
# meson.build (nsm66d)
#-----------------------------------------------------------------------------
##
# \file        meson.build
# \library     nsm66d
# \author      Chris Ahlstrom
# \date        2025-01-29
# \updates     2025-10-05
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm66" library. It was part of the libs66
#  collection of libraries, but is now separate and stands alone.
#
#  The "nsm66" library is free software: you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as published
#  by the Free Software Foundation, either version 3 of the License, or (at
#  your option) any later version.
#
#  The "nsm66" library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
#  Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with the "nsm66" library. If not, see <https://www.gnu.org/licenses/>.
#
#-----------------------------------------------------------------------------

project('nsm66d',
   [ 'c', 'cpp' ],
   version : '0.1',
   license : 'GPLv3',
   meson_version : '>=1.1.0',
   default_options : [
      'c_std=c11', 'cpp_std=c++14', 'warning_level=3',
      'default_library=both'
      ]
   )

#-----------------------------------------------------------------------------
# Conditional enabling of the Potext translation library.
#-----------------------------------------------------------------------------

nsm66d_use_potext = false
if get_option('potext')

   nsm66d_use_potext = true
   add_project_arguments('-DNSM66_USE_POTEXT', language : [ 'c', 'cpp' ])

endif

#-----------------------------------------------------------------------------
# Information for this project.
#-----------------------------------------------------------------------------
#
#  Tricky: the *.pc file must have a base name matching the name of the
#  subproject *.wrap file.
#
#-----------------------------------------------------------------------------

nsm66d_pkg_description = 'Refactors the nmsd NSM daemon modules.'
nsm66d_pkg_version = meson.project_version()
nsm66d_api_version = '0.1'
nsm66d_info_date = '2025-03-20'
nsm66d_info_build_type = get_option('buildtype')
nsm66d_info_build_root = meson.project_build_root()
nsm66d_info_lib_type = get_option('default_library')
nsm66d_info_project_root = meson.project_source_root()
nsm66d_debug = get_option('buildtype').contains('debug')
nsm66d_project_base = 'nsm66d-@0@'.format(nsm66d_api_version)
nsm66d_pkgconfig_name = 'nsm66d'

# -fno-inline-functions does not work.

if nsm66d_debug
   add_project_arguments(
      [ '-DDEBUG', '-fno-inline-functions' ],
      language : 'cpp'
   )
else
   add_project_arguments(
      [ '-DNDEBUG' ],
      language : 'cpp'
   )
endif

#-----------------------------------------------------------------------------
# Some defines for the other app projects (besides nsm66d).
#-----------------------------------------------------------------------------

jackpatch66_project_base = 'jackpatch66-@0@'.format(nsm66d_api_version)
nsm_proxy66_project_base = 'nsm_proxy66-@0@'.format(nsm66d_api_version)
nsmctl_project_base = 'nsmctl-@0@'.format(nsm66d_api_version)

#-----------------------------------------------------------------------------
# Easy access to directory options. Commented out the unnecessary ones.
# Many of these definitions are simply informative.
#-----------------------------------------------------------------------------

nsm66d_dir = 'nsm66d-@0@'.format(nsm66d_api_version)
nsm66d_prefix = get_option('prefix')
nsm66d_bindir = join_paths(nsm66d_prefix, get_option('bindir'))
nsm66d_includedir = join_paths(nsm66d_prefix, get_option('includedir'), nsm66d_dir)
nsm66d_libdir = join_paths(nsm66d_prefix, get_option('libdir'), nsm66d_dir)
nsm66d_datadir = join_paths(nsm66d_prefix, get_option('datadir'), nsm66d_dir)
nsm66d_docdir = join_paths(nsm66d_prefix, nsm66d_datadir, 'doc', nsm66d_dir)
libraries_top = meson.project_source_root()
nsm66d_include_top = join_paths(libraries_top, 'include')

#-----------------------------------------------------------------------------
# The *.pc file goes into $libdir/nsm66-0.x/pkgconfig instead of
# $libdir/pkgconfig as the meson documentation asserts.  Not sure why, but
# wiring the expected directory here works.
#-----------------------------------------------------------------------------
#
# alt_pkgconfig_libdir = join_paths(
#   nsm66d_prefix, get_option('libdir'), 'pkgconfig'
#   )

#-----------------------------------------------------------------------------
# nsm66d_headers provides the list of all headers in the project. These are
# currently all installable, public headers. (They don't need to be).
# They are set in include/meson.build.
#
# nsm66d_sources provides the source-code files. They are set in
# src/meson.build.
#-----------------------------------------------------------------------------

nsm66d_headers = []
nsm66d_sources = []
jackpatch66_headers = []
jackpatch66_sources = []
nsm_proxy66_headers = []
nsm_proxy66_sources = []
nsmctl_headers = []
nsmctl_sources = []
build_args = []

#-----------------------------------------------------------------------------
# Unlike the cfg66 library, this library will have platform-dependent
# characteristics.
#-----------------------------------------------------------------------------

os_unix     = false
os_freebsd  = false
os_linux    = false
os_win32    = false
os_darwin   = false

if host_machine.system() == 'darwin'
   os_darwin = true
elif host_machine.system() == 'windows'
   os_win32 = true
elif host_machine.system() == 'linux'
   os_linux = true
elif host_machine.system() == 'freebsd'
   os_freebsd = true
endif

os_unix = not os_win32

#-----------------------------------------------------------------------------
# Each include file directory is added to the list passed to the compiler.
# We prefer to use the style '#include "cfg/header.hpp".
#-----------------------------------------------------------------------------

nsm66d_includes = include_directories('.',
   'include',
   'include/nsmd'
   )

jackpatch66_includes = include_directories('.',
   'include',
   'include/jackpatch'
   )

nsm_proxy66_includes = include_directories('.',
   'include',
   'include/nsmproxy'
   )

nsmctl_includes = include_directories('.', 'include')

#-----------------------------------------------------------------------------
# Used by the nsm66d_version() function.
#-----------------------------------------------------------------------------
# Will clean this up in a bit.
#-----------------------------------------------------------------------------

build_args += [
   '-DNSM66D_NAME=' + '"' + meson.project_name() + '"',
   '-DNSM66D_VERSION=' + '"' + meson.project_version() + '"',
   '-DJACKPATCH66_NAME=' + '"jackpatch66"',
   '-DJACKPATCH66_VERSION=' + '"' + meson.project_version() + '"',
   '-DNSM_PROXY66_NAME=' + '"nsm-proxy66"',
   '-DNSM_PROXY66_VERSION=' + '"' + meson.project_version() + '"',
   '-DNSMCTL_NAME=' + '"nsmctl"',
   '-DNSMCTL_VERSION=' + '"' + meson.project_version() + '"',
   ]

#-----------------------------------------------------------------------------
# Only make public interfaces visible
#-----------------------------------------------------------------------------

'''
if target_machine.system() == 'windows' or target_machine.system() == 'cygwin'
   build_args += '-DLIBPUBLIC="__declspec(dllexport)"'
else
   build_args += '-DLIBPUBLIC=__attribute__((visibility("default")))'
endif
'''

#-----------------------------------------------------------------------------
# Set up some "HAVE" macros.  By default the header file is generated in the
# "build" directory. If configure_file() is moved to include/meson.build,
# the the header goes into "build/include", which is what we want.
#-----------------------------------------------------------------------------

cc = meson.get_compiler('cpp')
cdata = configuration_data()

cdata.set('limits_h', cc.has_header('limits.h'))
cdata.set('lo_h', cc.has_header('lo/lo.h'))

#-----------------------------------------------------------------------------
# Potential sub-projects
#-----------------------------------------------------------------------------
#
#  The dependencies here are generally non-OS specific.
#
#  Also see the meson.build files in the sub-directories of the 'src'
#  sub-directory.
#
#-----------------------------------------------------------------------------

if nsm66d_use_potext

   libpotext_dep = subproject('libpotext').get_variable('libpotext_dep')
   libpotext_library_dep = dependency(
      'libpotext', fallback : [ 'libpotext', 'libpotext_dep' ]
      )

endif

liblib66_dep = subproject('liblib66').get_variable('liblib66_dep')
liblib66_library_dep = dependency(
   'liblib66', fallback : [ 'liblib66', 'liblib66_dep' ]
   )

libcfg66_dep = subproject('libcfg66').get_variable('libcfg66_dep')
libcfg66_library_dep = dependency(
   'libcfg66', fallback : [ 'libcfg66', 'libcfg66_dep' ]
   )

libnsm66_dep = subproject('libnsm66').get_variable('libnsm66_dep')
libnsm66_library_dep = dependency(
   'libnsm66', fallback : [ 'libnsm66', 'libnsm66_dep' ]
   )

# libxpc66_library_dep = dependency(
#    'libxpc66', fallback : [ 'libxpc66', 'libxpc66_dep' ]
#    )

liblo_dep = dependency('liblo', required : true)
threads_dep = dependency('threads', required : true)

#-----------------------------------------------------------------------------
# Sub-directories. 'tests' has to be descended into later.
#-----------------------------------------------------------------------------

subdir('include')
subdir('src')

#-----------------------------------------------------------------------------
# Dependencies on Linux
#-----------------------------------------------------------------------------
#
# 'liblo', version : '>=1.0.0', required : true
#
#-----------------------------------------------------------------------------

empty_depends = [ ]

if os_linux

system_depends = [ liblo_dep, threads_dep ]

endif

#-----------------------------------------------------------------------------
# Make this library usable from the system's package manager.
#-----------------------------------------------------------------------------
#
# install_headers(libnsm66_headers, subdir : nsm66d_dir, preserve_path : true)

#-----------------------------------------------------------------------------
# Package config
#-----------------------------------------------------------------------------
#  Not needed for an application.
#-----------------------------------------------------------------------------

# TODO: figure out where to put and install the logo

install_data(
   files(
      'ChangeLog',
      'LICENSE.md',
      'README.md'
      ),
   install_dir : get_option('docdir')
   )

if get_option('enable-tests')
   subdir('tests')
endif

#-----------------------------------------------------------------------------
# Info
#-----------------------------------------------------------------------------

summary(
   {
      'Date' : nsm66d_info_date,
      'Filebase' : nsm66d_project_base,
      'Version' : nsm66d_pkg_version,
      'Base' : nsm66d_project_base,
      'Description' : nsm66d_pkg_description,
      'Source Root' : nsm66d_info_project_root,
      'Headers Root' : nsm66d_include_top,
      'Sub Dir' : nsm66d_project_base,
      'Build Type' : nsm66d_info_build_type,
      'Build Root' : nsm66d_info_build_root,
      'Library Type' : nsm66d_info_lib_type,
   },
   section : 'Nsm66 Project'
   )

summary(
   {
      'prefix' : nsm66d_prefix,
      'includedir' : nsm66d_includedir,
      'bindir' : nsm66d_bindir,
      'libdir' : nsm66d_bindir,
      'datadir' : nsm66d_datadir,
      'docdir' : nsm66d_docdir
   },
   section : 'Nsm66 Install Directories'
   )

#****************************************************************************
# meson.build (nsm66d)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
