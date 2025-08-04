#if ! defined NSM66_JACKPATCH66_HPP
#define NSM66_JACKPATCH66_HPP

/*
 * Copyright (C) 2008-2020 Jonathan Moore Liles (as "Non-Session-Manager")
 * Copyright (C) 2020- Nils Hilbricht
 *
 * This file is part of New-Session-Manager
 *
 * New-Session-Manager is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * New-Session-Manager is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with New-Session-Manager. If not, see <https://www.gnu.org/licenses/>
 */

/**
 * \file          jackpatch66.hpp
 *
 *    This module refactors the jackpatch application to replace C code with
 *    C++ code.
 *
 * \library       jackpatch66 application
 * \author        Chris Ahlstrom and other authors; see documentation
 * \date          2025-02-25
 * \updates       2025-03-07
 * \version       $Revision$
 * \license       GNU GPL v2 or above
 *
 *   To do.
 */

#include <string>                       /* std::string                      */
#include <vector>                       /* std::vector                      */

#include "c_macros.h"                   /* not_nullptr() etc.               */
#include "cpp_types.hpp"                /* lib66::tokenization alias        */

/*
 *  Needed for asprintf()
 */

#if ! defined GNU_SOURCE
#define GNU_SOURCE
#endif

/**
 *  Holds the client name and the port name for a client.
 *
 *  We're replacing most of the C code with C++ code.
 */

using client_port = struct
{
    std::string client;
    std::string port;
};

/**
 *  Holds information about the source and destination client-ports
 *  and if their connection active.
 *
 *  Operations on patch_record:
 *
 *  -   enqueue(patch_record p). This isn't a queue in the conventional
 *      sense of a FIFO. The patch-list starts empty. The first item (p0)
 *      points to the patch-list, which points to p0. The next item (p1)
 *      points to p0 and the patch-list points to p1. After 4 enqueue()
 *      calls:  patch-list --> p3 --> p2 --> p1 --> p0 --> nullptr
 *  -   dequeu(p): if p is not null, simply frees it. It is called only
 *      in clear_all_patches(), so we put the code there and remove this
 *      function.
 *  -   process_patch(). Scans a patch-record specification,
 *      creates a new patch_record, and passes it to enqueue(). Two are created
 *      if a "|" character is encountered, meaning bidirectional.
 *  -   connect_path(). Creates JACK source and destination port names and
 *      then connects the two ports.
 *  -   inactivate_path(). Sets the patch-record active flag to false.
 *  -   snapshot(). See it's function banner.
 *
 *  Why not use an std::list or and std::forward_list? Or even just vector?
 */

using patch_record = struct
{
    client_port pr_src;                 /* source client port               */
    client_port pr_dst;                 /* destination client port          */
    bool pr_active;                     /* true if patch is activated       */
};

/**
 *  Operations on port_record.
 *
 *  -   enqueue_known_port(). Creates a new port-record and hooks it to the
 *      existing known-port list in the same manner as enqueue(patch_record).
 *  -   remove_known_port(). Removes a port and marks it inactive in the other
 *      ports.
 *  -   find_known_port(). Iterates through the known-ports and returns
 *      the name of a match. Used only in the connect_path() function.
 */

using port_record = struct
{
    std::string port;
};

/**
 *  This structure is read from the JACK ring-buffer in dequeue_new_port()
 *  in check_for_new_ports.
 */

using port_notification_record = struct
{
    int pnr_length;                     /* len, the length of the ???       */
    int pnr_registered;                 /* reg: non-zero if registered      */
    char pnr_port [256];                /* char pnr_port[]?  TODO           */
};

/**
 *  A callback function type.
 */

using patchfunc = void (*) (patch_record &);

/**
 *  Provides a list of patch_records. There's no real need for a linked list
 *  (std::list or std::forward_list) for a set of data we anticipate to
 *  be a dozen or two. Note that vector<> and list<> share many of the
 *  same operations. One exception is sort().
 *
 *  Same for lists of port_records.
 *
 *      static patch_record * g_patch_list = nullptr;
 *      static port_record * g_known_ports = nullptr;
 */

using patch_list = std::vector<patch_record>;
using port_list = std::vector<port_record>;

#endif          // defined NSM66_JACKPATCH66_HPP

/*
 * jackpatch66.hpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */
