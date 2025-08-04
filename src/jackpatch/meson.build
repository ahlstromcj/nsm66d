#*****************************************************************************
# meson.build (src/jackpatch)
#-----------------------------------------------------------------------------
##
# \file        src/jackpatch/meson.build
# \library     jackpatch66
# \author      Chris Ahlstrom
# \date        2025-02-25
# \updates     2025-02-25
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "jackpatch66" application. See the top-level
#  meson.build file for license information.
#
#-----------------------------------------------------------------------------

if os_linux

liblo_dep = dependency('liblo', required : true)
threads_dep = dependency('threads', required : true)
jack_dep = dependency('jack', version : '>=1.9.0', required : true)
system_depends = [ liblo_dep, threads_dep, jack_dep ]

endif

if nsm66d_use_potext

   jackpatch66_exe_build = executable(
      jackpatch66_project_base,
      jackpatch66_sources,
      install : false,
      install_dir : nsm66d_bindir,
      c_args : build_args,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         liblib66_library_dep,
         libcfg66_library_dep,
         libnsm66_library_dep,
         libpotext_library_dep
         ],
      include_directories : [ jackpatch66_includes ]
      )

else

   jackpatch66_exe_build = executable(
      jackpatch66_project_base,
      jackpatch66_sources,
      install : false,
      install_dir : nsm66d_bindir,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         liblib66_library_dep,
         libcfg66_library_dep,
         libnsm66_library_dep
         ],
      include_directories : [ jackpatch66_includes ]
      )

endif

#****************************************************************************
# meson.build (jackpatch/src/jackpatch)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
