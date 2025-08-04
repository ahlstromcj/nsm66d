#*****************************************************************************
# meson.build (nsm66d/tests)
#-----------------------------------------------------------------------------
##
# \file        tests/meson.build
# \library     nsm66
# \author      Chris Ahlstrom
# \date        2025-01-29
# \updates     2025-03-05
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm66d" application. See the top-level
#  meson.build file for license information.
#
#-----------------------------------------------------------------------------

nsmd_test_exe = executable(
   'nsmd_test',
   sources : [ 'nsmd_test.cpp' ],
   dependencies : [
      liblib66_library_dep,
      libcfg66_library_dep,
      libnsm66_library_dep
      ]
   )

test('Nsm66d Test', nsmd_test_exe)
   
#****************************************************************************
# meson.build (nsm66d/tests)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------

