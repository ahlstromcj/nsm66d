#*****************************************************************************
# meson.build (nsm66d/src)
#-----------------------------------------------------------------------------
##
# \file        src/meson.build
# \library     nsm66d
# \author      Chris Ahlstrom
# \date        2025-01-29
# \updates     2025-03-25
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm66d" application. See the top-level
#  meson.build file for license information.
#
#  Here we locate the application source-code files.  They reside in:
#
#     -  src. The main include directory for non-namespace code.
#     -  src/nsmd. The main files of the "66" version of this application.
#     -  src/jackpatch. The main files of the "66" version of this application.
#     -  src/nsmproxy. The main files of the "66" version of this application.
#
#-----------------------------------------------------------------------------

nsm66d_sources += files(
   'nsm66d_version.cpp',
   'nsmd/nsm66d.cpp'
   )

jackpatch66_sources += files(
   'nsm66d_version.cpp',
   'jackpatch/jackpatch66.cpp'
   )

nsm_proxy66_sources += files(
   'nsm66d_version.cpp',
   'nsmproxy/nsm-proxy66.cpp'
   )

nsmctl_sources += files(
   'nsm66d_version.cpp',
   'nsmctl/nsmctl.cpp'
   )

subdir('jackpatch')
subdir('nsmd')
subdir('nsmproxy')
subdir('nsmctl')

#****************************************************************************
# meson.build (nsm66d/src)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
