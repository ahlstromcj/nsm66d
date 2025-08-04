#*****************************************************************************
# meson.build (src/nsmproxy)
#-----------------------------------------------------------------------------
##
# \file        src/nsmproxy/meson.build
# \library     nsm-proxy66
# \author      Chris Ahlstrom
# \date        2025-02-25
# \updates     2025-03-06
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm_proxy66" application. See the top-level
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

   nsm_proxy66_exe_build = executable(
      nsm_proxy66_project_base,
      nsm_proxy66_sources,
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
      include_directories : [ nsm_proxy66_includes ]
      )

else

   nsm_proxy66_exe_build = executable(
      nsm_proxy66_project_base,
      nsm_proxy66_sources,
      install : false,
      install_dir : nsm66d_bindir,
      cpp_args : build_args,
      dependencies : [
         system_depends,
         liblib66_library_dep,
         libcfg66_library_dep,
         libnsm66_library_dep
         ],
      include_directories : [ nsm_proxy66_includes ]
      )

endif

#****************************************************************************
# meson.build (jackpatch/src/jackpatch)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
