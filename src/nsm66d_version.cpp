/*
 *  This file is part of nsm66d.
 *
 *  nsm66 is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation; either version 2 of the License, or (at your option) any later
 *  version.
 *
 *  nsm66d is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with nsm66; if not, write to the Free Software Foundation, Inc., 59 Temple
 *  Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * \file          nsm66d_version.cpp
 *
 *  This module defines an informational function.
 *
 * \library       nsm66d application
 * \author        Chris Ahlstrom
 * \date          2025-01-29
 * \updates       2025-02-25
 * \license       GNU GPLv2 or above
 *
 */

#include "nsm66d_version.hpp"           /* no-namespace function library    */

/*
 * Version information strings. These are defined by build_args in the
 * top-level meson.build file.
 */

const std::string &
nsm66d_version () noexcept
{
    static std::string s_info = NSM66D_NAME "-" NSM66D_VERSION " " __DATE__ ;
    return s_info;
}

const std::string &
jackpatch66_version () noexcept
{
    static std::string s_info =
        JACKPATCH66_NAME "-" JACKPATCH66_VERSION " " __DATE__ ;

    return s_info;
}

const std::string &
nsmproxy66_version () noexcept
{
    static std::string s_info =
        NSM_PROXY66_NAME "-" NSM_PROXY66_VERSION " " __DATE__ ;

    return s_info;
}

/*
 * nsm66_version.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

