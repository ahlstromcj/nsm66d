#if ! defined NSM66_NSM66D_HPP
#define NSM66_NSM66D_HPP

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
 * \file          nsm66d.hpp
 *
 *    This module refactors the nsmd application to replace C code with
 *    C++ code.
 *
 * \library       nsm66d application
 * \author        Chris Ahlstrom and other authors; see documentation
 * \date          2025-02-05
 * \updates       2025-04-19
 * \version       $Revision$
 * \license       GNU GPL v2 or above
 *
 *   To do.
 */

/*
 * debug.c/h has only one function that gets used multiple times by debug.h
 * and for logging and printing. We don't use it; we use modules from the
 * cfg66 library project. Same for file.c/h.
 *
 *      #include "debug.h"
 *      #include "file.h"
 */

#if ! defined GNU_SOURCE
#define GNU_SOURCE
#endif

#include <algorithm>
#include <map>
#include <string>

#include "c_macros.h"                   /* not_nullptr() macro, etc.        */
#include "cpp_types.hpp"                /* lib66::tokenization alias        */
#include "nsm/nsmcodes.hpp"             /* nsm66: nsm::error, command enums */
#include "osc/endpoint.hpp"             /* osc::endpoint class              */
#include "util/strfunctions.hpp"        /* util::info_message() etc.        */

class Client
{

private:

    /**
     *  Storage for the latest error code. See the enumeration above.
     */

    int m_reply_errcode;

    std::string m_reply_message;

    int m_pending_command;

    struct timeval m_command_sent_time;

    bool m_gui_visible;

    std::string m_label;

public:

    /*
     * Pointer to an OSC address structure.
     *
     * See m_lo_address in nsm/nsmbase.
     */

    lo_address m_addr;

    /*
     * First this is the basename of client executable, later it becomes the
     * client-reported name which must be treated as if unrelated.
     */

    std::string m_name;

    /*
     * Contrary to the name this is basename(executable).
     */

    std::string m_exe_path;

    /*
     * PID of the client process represented by this Client object.
     */

    int m_pid;

    /*
     *  Progress indicator from client. What is the range?
     */

    float m_progress;

    /*
     * NSM capable: client has registered via announce.
     */

    bool m_active;

    /*
     * The client quit, but not because we told it to--user still has to
     * decide to remove it from the session.
     *
     *      bool stopped;
     */

    /*
     * Short part of client ID
     */

    std::string m_client_id;

    /*
     * Client capabilities... will be null for dumb clients.
     */

    std::string m_capabilities;

    /*
     * flag for client self-reported dirtiness.
     */

    bool m_dirty;
    bool m_pre_existing;
    std::string m_status;

    /*
     * v1.4, leads to status for executable not found, permission denied etc.
     */

    int m_launch_error;

    /*
     * v1.4, client.nABC
     */

    std::string m_name_with_id;

public:

    Client ();
    Client
    (
        const std::string & name,
        const std::string & exe,
        const std::string & id
    );

    ~Client () = default;

    bool has_error () const
    {
        return m_reply_errcode != 0;
    }

    int error_code () const
    {
        return m_reply_errcode;
    }

    void set_reply (int errcode, const std::string & message)
    {
        m_reply_errcode = errcode;
        m_reply_message = message;
    }

    const std::string & message ()
    {
        return m_reply_message;
    }

    bool reply_pending ()
    {
        return m_pending_command != nsm::command::none;
    }

    int pending_command ()
    {
        return m_pending_command;
    }

    void pending_command (int command);
    double ms_since_last_command () const;

    bool gui_visible () const
    {
        return m_gui_visible;
    }

    void gui_visible (bool b)
    {
        m_gui_visible = b;
    }

    const std::string & label () const
    {
        return m_label;
    }

    std::string & label ()
    {
        return m_label;
    }

    void label (const std::string & lbl)
    {
        m_label = lbl;
    }

    lo_address addr ()
    {
        return m_addr;
    }

    void addr (lo_address a)
    {
        m_addr = a;
    }

    const std::string & name () const
    {
        return m_name;
    }

    void name (const std::string & n)
    {
        m_name = n;
    }

    const std::string & exe_path () const
    {
        return m_exe_path;
    }

    void exe_path (const std::string & exe)
    {
        m_exe_path = exe;
    }

    int pid () const
    {
        return m_pid;
    }

    void pid (int p)
    {
        m_pid = p;
    }

    float progress () const
    {
        return m_progress;
    }

    void progress (float p)
    {
        m_progress = p;
    }

    bool active () const
    {
        return m_active;
    }

    void active (bool a)
    {
        m_active = a;
    }

    const std::string & client_id () const
    {
        return m_client_id;
    }

    void client_id (const std::string & id)
    {
        m_client_id = id;
    }

    const std::string & capabilities () const
    {
        return m_capabilities;
    }

    void capabilities (const std::string & c)
    {
        m_capabilities = c;
    }

    bool is_dumb_client ()
    {
        return m_capabilities.empty();
    }

    /*
     * capability should be enclosed in colons, as in ":switch:"
     */

    bool is_capable_of (const std::string & capability) const
    {
        return util::contains(m_capabilities, capability);
    }

    bool dirty () const
    {
        return m_dirty;
    }

    void dirty (bool d)
    {
        m_dirty = d;
    }

    bool pre_existing () const
    {
        return m_pre_existing;
    }

    void pre_existing (bool pe)
    {
        m_pre_existing = pe;
    }

    const std::string & status () const
    {
        return m_status;
    }

    void status (const std::string & s)
    {
        m_status = s;
    }

    int launch_error () const
    {
        return m_launch_error;
    }

    void launch_error (int p)
    {
        m_launch_error = p;
    }

    const std::string & name_with_id () const
    {
        return m_name_with_id;
    }

    void name_with_id (const std::string & n)
    {
        m_name_with_id = n;
    }

};              // class Client

using client_list = std::list<Client *>;

using client_map = std::map<std::string, int>;

#endif          // defined NSM66_NSM66D_HPP

/*
 * nsm66d.hpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

