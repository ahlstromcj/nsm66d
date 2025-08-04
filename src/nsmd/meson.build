#*****************************************************************************
# meson.build (nsm66d/src/nsmd)
#-----------------------------------------------------------------------------
##
# \file        src/nsmd/meson.build
# \library     nsm66d
# \author      Chris Ahlstrom
# \date        2025-02-14
# \updates     2025-02-14
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm66d" application. See the top-level
#  meson.build file for license information.
#
#-----------------------------------------------------------------------------

if os_linux

liblo_dep = dependency('liblo', required : true)
threads_dep = dependency('threads', required : true)
system_depends = [ liblo_dep, threads_dep ]

endif

if nsm66d_use_potext

   nsm66d_exe_build = executable(
      nsm66d_project_base,
      nsm66d_sources,
      install : false,
      install_dir : nsm66d_bindir,
      c_args : build_args,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         libcfg66_library_dep,
         liblib66_library_dep,
         libnsm66_library_dep,
         libpotext_library_dep
         ],
      include_directories : [ nsm66d_includes ]
      )

else

   nsm66d_exe_build = executable(
      nsm66d_project_base,
      nsm66d_sources,
      install : false,
      install_dir : nsm66d_bindir,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         libcfg66_library_dep,
         liblib66_library_dep,
         libnsm66_library_dep
         ],
      include_directories : [ nsm66d_includes ]
      )

endif

#****************************************************************************
# meson.build (nsm66d/src/nsmd)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
