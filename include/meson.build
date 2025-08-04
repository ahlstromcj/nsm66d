#*****************************************************************************
# meson.build (nsm66d/include)
#-----------------------------------------------------------------------------
##
# \file        include/meson.build
# \library     nsm66d
# \author      Chris Ahlstrom
# \date        2025-01-29
# \updates     2025-03-06
# \license     $XPC_SUITE_GPL_LICENSE$
#
#  This file is part of the "nsm66d" application. See the top-level
#  meson.build file for license information.
#
#  Here we locate the headers. The root of the header directory is 'include'.
#
#-----------------------------------------------------------------------------

nsm66d_headers += files(
   'nsm66d_version.hpp',
   'nsmd/nsm66d.hpp'
   )

jackpatch66_headers += files(
   'nsm66d_version.hpp',
   'jackpatch/jackpatch66.hpp'
   )

nsm_proxy66_headers += files(
   'nsm66d_version.hpp',
   'nsmproxy/nsm-proxy66.hpp'
   )

configure_file(
   input : 'nsm66d-config.h.in',
   output : 'nsm66d-config.h',
   configuration : cdata
   )

#****************************************************************************
# meson.build (nsm66d/include)
#----------------------------------------------------------------------------
# vim: ts=3 sw=3 ft=meson
#----------------------------------------------------------------------------
