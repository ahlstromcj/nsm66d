#*****************************************************************************
# meson.build (nsmctl/src/nsmd)
#-----------------------------------------------------------------------------
##
# \file        src/nsmd/meson.build
# \library     nsmctl
# \author      Chris Ahlstrom
# \date        2025-03-25
# \updates     2025-03-25
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsmctl" application. See the top-level
#  meson.build file for license information.
#
#-----------------------------------------------------------------------------

if os_linux

liblo_dep = dependency('liblo', required : true)
threads_dep = dependency('threads', required : true)
system_depends = [ liblo_dep, threads_dep ]

endif

if nsm66d_use_potext

   nsmctl_exe_build = executable(
      nsmctl_project_base,
      nsmctl_sources,
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
      include_directories : [ nsmctl_includes ]
      )

else

   nsmctl_exe_build = executable(
      nsmctl_project_base,
      nsmctl_sources,
      install : false,
      install_dir : nsm66d_bindir,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         libcfg66_library_dep,
         liblib66_library_dep,
         libnsm66_library_dep
         ],
      include_directories : [ nsmctl_includes ]
      )

endif

#****************************************************************************
# meson.build (nsmctl/src/nsmd)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
