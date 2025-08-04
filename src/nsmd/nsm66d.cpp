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
 * \file          nsm66d.cpp
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

#include <cctype>                       /* std::isupper()                   */
#include <cerrno>                       /* #include <errno.h>               */
#include <csignal>                      /* std::signal() and <signal.h>     */
#include <cstring>                      /* std::strerror()                  */
#include <cstdlib>                      /* std::getenv(), std::rand()       */

#include <fts.h>                        /* function to traverse directories */
#include <getopt.h>                     /* GNU get command-line option      */
#include <stdio.h>                      /* vasprintf()                      */
#include <sys/signalfd.h>               /* struct signalfd_siginfo          */
#include <sys/time.h>                   /* getttimeofday()                  */
#include <sys/wait.h>                   /* wait() or waitpid()              */
#include <unistd.h>                     /* execvp()                         */

#include "nsm66d.hpp"                   /* err-codes, Client class, etc.    */
#include "cfg/appinfo.hpp"              /* cfg66: cfg::set_client_name()    */
#include "osc/messages.hpp"             /* nsm66: osc::tag enumeration      */
#include "nsm/helpers.hpp"              /* nsm66: nsm::session_triplets     */
#include "util/msgfunctions.hpp"        /* cfg66: util::string_asprintf()   */
#include "util/filefunctions.hpp"       /* cfg66: util::file_write_lines()  */
#include "util/ftswalker.hpp"           /* cfg66: util::fts_copy_direc...() */

#define NSMD66_APP_NAME                 "nsm66d"
#define NSMD66_APP_TITLE                "Nsmd 66"
#define NSMD_VERSION_STRING             "1.6.1"

static client_list s_client_list;
static osc::endpoint * s_osc_server;
static lo_address s_gui_address;
static bool s_gui_is_active{false};
static std::string s_session_root;
static std::string s_session_path;
static std::string s_session_name;
static std::string s_lockfile_directory;
static std::string s_daemon_file;
static nsm::command s_pending_operation = nsm::command::none;

static std::string s_session_subdir{"nsm"};
static std::string s_session_file{"session.nsm"};
static std::string s_path_fmt{"%s/session.nsm"};
static std::string s_full_path_fmt{"%s/%s/session.nsm"};

/*-------------------------------------------------------------------------
 * Client functions
 *-------------------------------------------------------------------------*/

Client::Client () :
    m_reply_errcode     (0),
    m_reply_message     (),
    m_pending_command   (0),
    m_command_sent_time (),
    m_gui_visible       (true),
    m_label             (),
    m_addr              (),
    m_name              (),
    m_exe_path          (),
    m_pid               (0),
    m_progress          (-0.0f),
    m_active            (false),
    m_client_id         (),
    m_capabilities      (),
    m_dirty             (false),
    m_pre_existing      (false),
    m_status            (),
    m_launch_error      (nsm::error::ok),
    m_name_with_id      ()
{
    // no other code
}

Client::Client
(
    const std::string & name,
    const std::string & exe,
    const std::string & id
) :
    m_reply_errcode     (0),
    m_reply_message     (),
    m_pending_command   (0),
    m_command_sent_time (),
    m_gui_visible       (true),
    m_label             (),
    m_addr              (),
    m_name              (name),
    m_exe_path          (exe),
    m_pid               (0),
    m_progress          (-0.0f),
    m_active            (false),
    m_client_id         (id),
    m_capabilities      (),
    m_dirty             (false),
    m_pre_existing      (false),
    m_status            (),
    m_launch_error      (nsm::error::ok),
    m_name_with_id      ()
{
    // no other code
}

void
Client::pending_command (int command)
{
    gettimeofday(&m_command_sent_time, NULL);
    m_pending_command = command;
}

double
Client::ms_since_last_command () const
{
    struct timeval now;
    gettimeofday(&now, NULL);

    double elapsedms = (now.tv_sec - m_command_sent_time.tv_sec) * 1000.0;
    elapsedms += (now.tv_usec - m_command_sent_time.tv_usec) / 1000.0;
    return elapsedms;
}

/*-------------------------------------------------------------------------
 * Helper functions
 *-------------------------------------------------------------------------*/

void handle_signal_clean_exit (int signal);     /* defined way below        */

/*
 * GUI sends:
 *
 *  send(s_gui_addr, cmdpath, client ID, client label) has many calls.
 *  send(lo_message_get_source(), /error|/reply, path, errorcode, message)
 */

void
gui_send (const char * msg, const std::string & s1, const std::string & s2)
{
    if (s_gui_is_active)
        s_osc_server->send(s_gui_address, msg, CSTR(s1), CSTR(s2));
}

void
error_send
(
    lo_message msg, const std::string & path,
    int errcode, const char * errmsg
)
{
    s_osc_server->send
    (
        lo_message_get_source(msg), "/error", CSTR(path), errcode, errmsg
    );
}

void
error_send_ex
(
    lo_message msg, const std::string & path,
    int errcode, const char * errmsg
)
{
    lo_address senderaddr = lo_address_new_from_url
    (
        lo_address_get_url(lo_message_get_source(msg))
    );
    util::warn_message(std::string(errmsg));
    s_osc_server->send
    (
        senderaddr, "/error", CSTR(path), errcode, errmsg
    );
}

void
reply_send
(
    lo_message msg, const std::string & path,
    const char * replymsg
)
{
    s_osc_server->send
    (
        lo_message_get_source(msg), "/reply", CSTR(path), replymsg
    );
}

void
reply_send_ex
(
    lo_message msg, const std::string & path,
    const char * replymsg
)
{
    lo_address senderaddr = lo_address_new_from_url
    (
        lo_address_get_url(lo_message_get_source(msg))
    );
    util::info_message("Reply", std::string(replymsg));
    s_osc_server->send
    (
        senderaddr, "/reply", CSTR(path), replymsg
    );
}

/*
 * Signal handling.
 *
 *  We consolidate code to hide the detaila of waiting for a SIGCHLD
 *  signal.
 */

void handle_sigchld ();

int
signal_descriptor ()
{
    static int s_signal_descriptor = (-1);          /* s_signal_fd */
    static bool s_uninitialized = true;
    if (s_uninitialized)
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        s_signal_descriptor = signalfd(-1, &mask, SFD_NONBLOCK);
        s_uninitialized = false;
    }
    return s_signal_descriptor;
}

/*
 *  This function reads the global signal file descriptor. If the signal
 *  is SIGCHLD, then this function calls handle_sigchld().
 *
 *  The signalfd_siginfo structure has a lot of members, but we use only
 *  one so far.
 */

void
handle_child_signal ()
{
    int sd = signal_descriptor();
    struct signalfd_siginfo fdsi;
    ssize_t s = read(sd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s == sizeof(struct signalfd_siginfo))
    {
        if (fdsi.ssi_signo == SIGCHLD)
            handle_sigchld();
    }
}

/*
 *  W have two variadic functions, foo(format, ...) and bar(format, ...).
 *  We want to implement function foo() so that it can invoke bar() with
 *  the same list of arguments it has. That is,
 *
 *      foo(format...)
 *      {
 *         ...
 *         bar(format, ...);
 *      }
 *
 * We have to plan ahead and implement each variadic function in two-staged
 * fashion. Once we have each of our variadic functions implemented through
 * a pair of va_list * function and ... function, you can delegate the calls
 * using the va_list * versions of the functions.
 */

static void
gui_msg_send (const std::string & format, va_list vargs)
{
    if (s_gui_is_active)
    {
        char * s;
        int count = vasprintf(&s, CSTR(format), vargs);
        if (count != (-1))
        {
            s_osc_server->send(s_gui_address, "/nsm/gui/server/message", s);
            free(s);
        }
    }
}

void
gui_msg (std::string format, ...)
{
    va_list vargs;
    va_start(vargs, format);
    gui_msg_send(format, vargs);
    va_end(vargs);
}

/*-------------------------------------------------------------------------
 * Application functions
 *-------------------------------------------------------------------------*/

bool
clients_have_errors ()
{
    for (auto c : s_client_list)
    {
        if (c->active() && c->has_error())
            return true;
    }
    return false;
}

Client *
get_client_by_pid (int pid)
{
    for (auto c : s_client_list)
    {
        if (c->pid() == pid)
            return c;
    }
    return nullptr;
}

/**
 *  Need to do it the brute force way. Can't get auto to work; we
 *  need the iterator.
 *
 *  Is there a better way, like client_list::clear()?
 */

void
clear_clients ()
{
    client_list & cl = s_client_list;
    for (client_list::iterator i = cl.begin(); i != cl.end(); ++i)
    {
        delete *i;
        i = cl.erase(i);
    }
}

/*
 * There is a difference if a client quit on its own, e.g. via a menu or window
 * manager, or if the server send SIGTERM as quit signal. Both cases are equally
 * valid.  We only check the case to print a different log message.
 */

void
handle_client_process_death (int pid)
{
    Client * c = get_client_by_pid(int(pid));
    if (not_nullptr(c))
    {
        bool dead_because_we_said =
        (
            c->pending_command() == nsm::command::kill ||
            c->pending_command() == nsm::command::quit
        );
        if (dead_because_we_said)
        {
            gui_msg
            (
                "Client %s terminated by server", CSTR(c->name_with_id())
            );
        }
        else
        {
            gui_msg
            (
                "Client %s terminated itself", CSTR(c->name_with_id())
            );
        }

        /*
         * Decide if the client terminated or if removed from the session
         */

        if (c->pending_command() == nsm::command::quit)
        {
            c->status("removed");
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());

            /*
             *  This will not remove the client's save-data.
             */

            s_client_list.remove(c);
            delete c;
        }
        else
        {
           /*
            * NSM API treats the stopped status as switch. You can only
            * remove stopped clients. Furthermore the GUI will change its
            * client-buttons. In consequence, we cannot add an arbitrary
            * "launch-error" status. A compatible compromise is to use the
            * label field to relay info the user, which was the goal.
            * There is nothing we can do about a failed launch anyway.
            */

            if (c->launch_error())
                c->label("Launch error!");
            else
                c->label().clear();

            c->status("stopped");
            if (s_gui_is_active)
            {
                gui_send
                (
                    "/nsm/gui/client/label", c->client_id(), c->label()
                );
                gui_send
                (
                    "/nsm/gui/client/status", c->client_id(), c->status()
                );
            }
        }
        c->pending_command(nsm::command::none);
        c->active(false);
        c->pid(0);
    }
}

/*
 *  Make it not NULL to enable information storage in status.
 *  Note that -1 means wait for any child process. pid_t is a signed
 *  integer.
 *
 *  Compare to waitpid(2).
 */

void
handle_sigchld ()
{
    for (;;)
    {
        int status = 1;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
        {
            break;      /* no child process ended this loop; check again    */
        }
        else
        {
            /*
             * One child process has stopped. Find which and figure out the
             * stop-conditions.
             */

            Client * c = get_client_by_pid(pid);
            if (not_nullptr(c))
            {
                /*
                 * The following will not trigger with normal crashes, e.g.
                 * segfaults or python tracebacks.
                 */

                if (WIFEXITED(status)) /* true if child terminated normally */
                {
                    if (WEXITSTATUS(status) == 255) /* exit(-1) in launch() */
                        c->launch_error(true);
                }
            }

            /*
             * Call even if Client was already null. This will check itself
             * again and was expected. To be called for the majority of
             * nsmd's development.
             */

            handle_client_process_death(pid);
        }
    }
}

/**
 *  This function surrounds the given path with slashes. It returns
 *  true if the result does not contain "/../".  Investigate!
 *
 *  See session_already_exists().
 *
 *  Make this more C++ later.
 */

bool
path_is_valid (const std::string & path)
{
#if 0
    char * s;
    asprintf(&s, "/%s/", V(path));

    bool result = strstr(s, "/../") == NULL;
    free(s);
#else
    bool result = ! util::contains(path, "..");
#endif
    return result;
}

/*
 *  A session is defined as a path with the file "session.nsm". The full
 *  format is "%s/%s/session.nsm".
 *
 *  We receive the relative path with sub-directories like album/song as
 *  relativepath, without leading and trailing "/".
 */

bool
session_already_exists (const std::string & relativepath)
{
    std::string path = util::string_asprintf
    (
        s_full_path_fmt, V(s_session_root), V(relativepath)
    );
    return util::file_exists(path);
}

void
set_name (const std::string & name)
{
    s_session_name = util::filename_base(name);
}

bool
address_matches (lo_address addr1, lo_address addr2)
{
    std::string url1 = lo_address_get_port(addr1);
    std::string url2 = lo_address_get_port(addr2);
    return url1 == url2;
}

/**
 *  We need a way to look up by client ID or name. This function
 *  returns true if the parameter matches the template "nXXXX".
 */

bool
is_a_client_id (const std::string & s)
{
    bool result = s.size() == 5;
    if (result)
    {
        result = s[0] == 'n';
        if (result)
        {
            result =
            (
                std::isupper(s[1]) && std::isupper(s[2]) &&
                std::isupper(s[3]) && std::isupper(s[4])
            );
        }
    }
    return result;
}

/**
 * NEW 2025-04-11
 */

Client *
get_client_by_name
(
    const client_list & cl,
    const std::string & name
)
{
    for (auto & c : cl)
    {
        if (c->name() == name)
            return c;
    }
    return nullptr;
}

Client *
get_client_by_id
(
    const client_list & cl,
    const std::string & id
)
{
    if (is_a_client_id(id))
    {
        for (auto & c : cl)
        {
            if (c->client_id() == id)
                return c;
        }
    }
    else
    {
        return get_client_by_name(cl, id);
    }
    return nullptr;
}

Client *
get_client_by_name_and_id
(
    const client_list & cl,
    const std::string & name,
    const std::string & id
)
{
    for (auto & c : cl)
    {
        if (c->client_id() == id && c->name() == name)
            return c;
    }
    return nullptr;
}

Client *
get_client_by_address (lo_address addr)
{
    for (auto & c : s_client_list)
    {
        if (address_matches(c->addr(), addr))
            return c;
    }
    return nullptr;
}

/**
 *  Before v1.4 this returned "n" + 4 random upper-case letters, which could
 *  lead to collisions.  We changed behaviour to still generate 4 letters,
 *  but check for collision with existing IDs.
 *
 *  Loaded client IDs are not checked, just copied from "session.nsm" because
 *  loading happens before any generation of new clients. Loaded clients are
 *  part of further checks of course.
 *
 *  There is a theoretical limit when all 26^4 IDs are in use which will lead
 *  to an infinite loop of generation. We risk leaving this unhandled.
 *
 *  The generate_client_id() function has been replaced with:
 *
 *          nsm::generate_client_id("n----");
 */

/**
 *  Searches for active clients with a status of reply-pending.
 */

bool
replies_still_pending ()
{
    for (const auto & c : s_client_list)
    {
        if (c->active() && c->reply_pending())
            return true;
    }
    return false;
}

/**
 *  This was renamed from number_of_active_clients() in version 1.4 to reflect
 *  that not only active == true clients are in a state where waiting has
 *  ended, but also clients that never started. It is used in
 *  wait_for_announce() only, which added a 5000ms delay to startup.
 *
 *  We are sadly unable to distinguish between a client that has a slow
 *  announce and a client without NSM-support. However, this is mitigated by
 *  nsm-proxy which is a reliable indicator that this program will never
 *  announce (or rather nsm-proxy announces normally).
 *
 *  Optimisation:
 *
 *      Clients that never launched (e.g. file not found) will be checked
 *      many times/second here. We skip them by counting them.
 */

int
number_of_reponsive_clients ()
{
    int responsive = 0;
    for (const auto & c : s_client_list)
    {
        if (c->active() || c->launch_error())
            ++responsive;
    }
    return responsive;
}

bool
process_is_running (int pid)
{
    if (kill(pid, 0) == 0)
    {
        return true;
    }
    else if (ESRCH == errno)
    {
        return false;
    }
    return false;
}

void
purge_dead_clients ()
{
    client_list tmp(s_client_list);     /* why a copy?  */
    for (const auto & c : tmp)
    {
        if (c->pid())
        {
            if (! process_is_running(c->pid()))
                handle_client_process_death(c->pid());
        }
    }
}

void
wait (long timeout)
{
    handle_child_signal();              /* uses signalfd_siginfo and read() */
    s_osc_server->wait(timeout);
    purge_dead_clients();
}

void
wait_for_announce ()
{
    int n = 5 * 1000;
    gui_msg("Waiting for announcements from clients");
    size_t active = 0;
    while (n > 0)
    {
        n -= 100;
        wait(100);
        active = size_t(number_of_reponsive_clients());
        if (s_client_list.size() == active)
            break;
    }
    gui_msg
    (
        "Done. %lu out of %lu clients announced (or failed to launch) "
        "within the initialization grace period",
        active, s_client_list.size()
    );
}

void
wait_for_replies ()
{
    int n = 60 * 1000;                                          /* 60 seconds */
    gui_msg("Waiting for clients to reply to commands");
    while (n > 0)
    {
        n -= 100;
        wait(100);
        if (! replies_still_pending())
            break;
    }
    gui_msg("Done waiting");

    /* FIXME: do something about unresponsive clients */
}

std::string
get_client_project_path (const std::string & session_path, Client * c)
{
    return util::string_asprintf
    (
        "%s/%s.%s", V(session_path), V(c->name()), V(c->client_id())
    );
}

/*
 *  Notes:
 *
 *      1. After the fork(): This is code of the child process. It will
 *         be executed after launch() has finished.
 *      2. Ensure the launched process can receive SIGCHLD.  Unblocking
 *         SIGCHLD here does NOT unblock it for nsmd itself.
 *      3. The program was not started. Causes: not installed on the
 *         current system, and the session was transferred from another
 *         system, or permission denied (no executable flag).
 *         Since we are running in a forked child process, Client c does
 *         exist, but points to a memory copy, not the real client.
 *         So we can't set any error code or status in the client object.
 *         Instead we check the exit return code in handle_sigchld() and
 *         set the bool client->launch_error to true.
 *      4. This is code of the parent process. It is executed right at this
 *         point, before the child.
 *      5. Normal launch. Setting launch_error to false is not redundant:
 *         a previous launch-error fixed by the user, and then resume,
 *         needs this reset.
 *      6. At this point, we do not know if launched program will start or
 *         fail. And we do not know if it has nsm-support or not. This will
 *         be decided if it announces.
 */

bool
launch (const std::string & executable, const std::string & clientid)
{
    Client * c = get_client_by_id(s_client_list, clientid);
    if (is_nullptr(c))
    {
        std::string base { basename(STR(executable)) } ;
        std::string id
        {
            clientid.empty() ? nsm::generate_client_id("n----") : clientid
        };
        c = new (std::nothrow) Client(base, executable, id);
        if (not_nullptr(c))
        {
            c->name_with_id
            (
                util::string_asprintf("%s.%s",
                V(c->name()), V(c->client_id()))
            );
            s_client_list.push_back(c);
        }
        else
        {
            util::error_message("Could not create client, aborting", base);
            return false;
        }
    }

    std::string url = s_osc_server->url();
    int pid;
    if (! (pid = fork()))                               /* see Note 1       */
    {
        char * const args [] = { STR(executable), nullptr };
        gui_msg("Launching %s", V(executable));
        setenv("NSM_URL", CSTR(url), 1);

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_UNBLOCK, &mask, NULL);          /* see Note 2       */
        errno = 0;                                      /* strerror(3)      */
        if (execvp(CSTR(executable), args) == (-1))     /* see Note 3       */
        {
            int ec = errno;
            util::error_printf
            (
                "Error starting process %s: %s",
                V(executable), std::strerror(ec)
            );
            exit(-1);                                   /* i.e. 255         */
        }
    }

    /*
     * Parent process code.
     */

    c->pending_command(nsm::command::start);            /* see Note 4       */
    c->pid(pid);                                        /* set client's PID */
    util::info_printf
    (
        "Process %s has pid: %i", V(executable), pid    /* no name yet      */
    );
    c->launch_error(false);                             /* see Note 5       */
    c->status("launch");

    /*
     * A second message may get send with c->name, if the client sends
     * announce(). See Note 6. The tab names of the messages are:
     * tag::guinew, tag::guistatus, and tag::guilabel.
     */

    gui_send("/nsm/gui/client/new", c->client_id(), c->exe_path());
    gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    gui_send("/nsm/gui/client/label", c->client_id(), "");
    return true;
}

void
command_client_to_save (Client * c)
{
    if (c->active())
    {
        util::info_printf("Telling %s to save", V(c->name_with_id()));
        s_osc_server->send(c->addr(), "/nsm/client/save");
        c->pending_command(nsm::command::save);
        c->status("save");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
    else if (c->is_dumb_client() && c->pid() > 0)
    {
        c->status("noop");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
}

void
command_client_to_switch (Client * c, const std::string & new_client_id)
{
    std::string old_client_id = c->client_id();
    c->client_id(new_client_id);

    std::string client_project_path = get_client_project_path
    (
        s_session_path, c
    );
    util::info_printf
    (
        "Commanding %s to switch \"%s\"",
        V(c->name_with_id()), V(client_project_path)
    );

    std::string full_client_id = util::string_asprintf
    (
        "%s.%s", V(c->name()), V(c->client_id())
    );
    s_osc_server->send                  /* osc::tag;;cliopen                */
    (
        c->addr(), "/nsm/client/open", client_project_path,
        CSTR(s_session_name), CSTR(full_client_id)
    );
    c->pending_command(nsm::command::open);
    c->status("switch");                /* osc::tag::guistatus, guiswitch   */
    gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    gui_send("/nsm/gui/client/switch", old_client_id, c->client_id());
}

void
purge_inactive_clients ()
{
    client_list & cl = s_client_list;
    for (client_list::iterator i = cl.begin(); i != cl.end(); ++i)
    {
        Client * c = *i;
        if (! c->active())
        {
            c->status("removed");
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());
            delete *i;
            i = s_client_list.erase(i);
        }
    }
}

/*--------------------------------------------------------------------------
 * OSC Message Handlers
 *--------------------------------------------------------------------------*/

OSC_HANDLER( add )
{
    (void) types; (void) user_data;             /* hide unused parameters   */
    if (argc >= 1)
    {
        if (s_session_path.empty())
        {
            error_send
            (
                msg, path, nsm::error::no_session_open,
                "Cannot add to session because no session is loaded"
            );
            return osc::osc_msg_handled();
        }

        /*
         * Tricky: argv[0] is a lo_arg, a union of various simple data
         * types like float and char. The s value's data type is "char",
         * but it is used for holding a null-terminated string.
         *
         * std::string clientname =
         *    std::string(reinterpret_cast<const char *>(&argv[0]->s));
         *
         * std::string clientname = &argv[0]->s;
         */

        std::string clientname = osc::string_from_lo_arg(argv[0]);
        if (util::name_has_path(clientname))
        {
            error_send
            (
                msg, path, nsm::error::launch_failed,
                "Paths not permitted; clients must be in $PATH"
            );
            return osc::osc_msg_handled();
        }
        if (launch(clientname, NULL))
        {
            reply_send(msg, path, "Launched");
        }
        else
        {
            error_send
            (
                msg, path, nsm::error::launch_failed, "Failed to launch process"
            );
        }
    }
    return osc::osc_msg_handled();
}

/**
 *  A client announces itself which identifies it as real nsm-capable client,
 *  internally represented by the c->active bool. These announcements
 *  have been moved to the osc::process_announce() function defined
 *  in the nsm66 library's lowrapper module.
 *
 *  If nsmd started the client itself (e.g. through a GUI) at this point the
 *  program is already part of the session and registered with
 *  c->name=basename(executable). For these clients a second client/new
 *  message is sent, indicating an upgrade of the formerly dumb client.
 *  Through this c->name changes from executable to the self-reported client
 *  name from this announce message.
 *
 *  Before v1.4 clients that announce themselves (started with NSM URL
 *  ENV present) never triggered the first client/new which sends an
 *  executable.  This created a problem with attaching GUIs to a running
 *  nsmd never were able to infer any data from executables, like icons.
 *  Changed so that every new client scenario sends basename(executable)
 *  first.
*/

OSC_HANDLER( announce )
{
    (void) types; (void) user_data;             /* hide unused parameters   */
    if (argc >= 6)
    {
        const std::string & clientname = &argv[0]->s;
        const std::string & caps = &argv[1]->s;
        const std::string & exe = &argv[2]->s;
        int major = argv[3]->i;
        int minor = argv[4]->i;
        int pid = argv[5]->i;
        gui_msg("Announce from %s", CSTR(clientname));
        util::info_message("Announce from", clientname);
        if (s_session_path.empty())
        {
            error_send
            (
                msg, path, nsm::error::no_session_open,
                "No session open for this application to join"
            );
            return osc::osc_msg_handled();
        }

        bool expected_client = false;
        Client * c = nullptr;
        for (auto & ci : s_client_list)
        {
            if
            (
                ci->exe_path() == exe && ! ci->active() &&
                ci->pending_command() == nsm::command::start
            )
            {
                /*
                 * We think we've found the slot we were looking for.
                 */

                util::info_message("Client was expected", ci->name());
                c = ci;
                break;
            }
        }
        if (is_nullptr(c))
        {
            c = new (std::nothrow) Client();
            c->exe_path(exe);           /* executable path from argv[2]     */
            c->client_id(nsm::generate_client_id("n----"));
        }
        else
            expected_client = true;

        if (major > NSM_API_VERSION_MAJOR)
        {
            util::warn_printf
            (
                "Client %s is using incompatible recent API version %i.%i",
                V(c->name_with_id()), major, minor
            );
            error_send
            (
                msg, path, nsm::error::incompatible_api,
                "Server is using an incompatible API version"
            );
            return osc::osc_msg_handled();
        }
        c->pid(pid);                    /* PID comes from argv[5]           */
        c->capabilities(caps);          /* capabilities from argv[1]        */
        c->addr
        (
            lo_address_new_from_url
            (
                lo_address_get_url(lo_message_get_source(msg))
            )
        );

        /*
         * Replace executable's name with the clients self-reported pretty name.
         */

        c->name(clientname);            /* client name from argv[0]         */
        c->active(true);
        c->name_with_id
        (
            util::string_asprintf("%s.%s", V(c->name()), V(c->client_id()))
        );
        util::info_printf("Process %s has pid: %i", V(c->name_with_id()), pid);
        if (! expected_client)
            s_client_list.push_back(c);

        util::info_printf
        (
            "Client \"%s\" at \"%s\" informs it's ready to receive commands",
            &argv[0]->s, lo_address_get_url(c->addr())
        );

        const char * ack1 = "Ack'ed as NSM client (started ourselves)";
        const char * ack2 =
            "Ack'ed as NSM client (registered itself from the outside)";

        s_osc_server->send                  /* too many args for reply_send */
        (
            lo_message_get_source(msg), "/reply", path,
            expected_client ? ack1 : ack2,
            NSMD66_APP_TITLE,
            ":server-control:broadcast:optional-gui:"
        );
        c->status("open");
        if (s_gui_is_active)
        {
            gui_send("/nsm/gui/client/new", c->client_id(), c->name());
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());
            if (c->is_capable_of(":optional-gui:"))
            {
                s_osc_server->send
                (
                    s_gui_address, "/nsm/gui/client/has_optional_gui",
                    CSTR(c->client_id())
                );
            }
        }

        std::string full_client_id = util::string_asprintf
        (
           "%s.%s", V(c->name()), V(c->client_id())
        );
        std::string client_project_path = get_client_project_path
        (
            s_session_path, c
        );
        s_osc_server->send
        (
            lo_message_get_source(msg), "/nsm/client/open",
            CSTR(client_project_path), CSTR(s_session_name),
            CSTR(full_client_id)
        );
        c->pending_command(nsm::command::open);
    }
    return osc::osc_msg_handled();
}

/**
 *  The session file is a list of clients in the following format:
 *
 *      "clientname:executablepath:clientid"
 *
 *      -   clientname. The name given to the client by the user, such as
 *          "seq66".
 *      -   executablepath. The name needed to execute the client, such
 *          as "qseq66". Can include a path, one would assume.
 *      -   clientidd. The unique identifier for the client in the session,
 *          such as "nWXYZ".
 *
 *          "seq66:qseq66:seq66.nWXYZ"
 *
 *  The path format is "%s/session.nsm".
 *
 *  We could use filefunctions to assemble the file-name.
 */

int
save_session_file ()
{
    std::string sessionfile = util::string_asprintf
    (
        s_path_fmt, V(s_session_path)
    );
    lib66::tokenization textlist;
    for (const auto & c : s_client_list)
    {
        std::string line = util::string_asprintf
        (
            "%s:%s:%s", V(c->name()), V(c->exe_path()), V(c->client_id())
        );
        textlist.push_back(line);
    }
    bool result = util::file_write_lines(sessionfile, textlist);
    return result ? 0 : 1 ;
}

/*
 *  We should use a reference to the client list, not a pointer.
 */

Client *
client_by_name (const std::string & name, client_list & cl)
{
    for (const auto & c : cl)
    {
        if (c->name() == name)
            return c;
    }
    return nullptr;
}

/*
 *  This replaced the Loop 1, Loop 2 ... 60 message from
 *  wait_for_dumb_clients_to_die(), where one couldn't see which client
 *  actually was hanging.
 */

bool
dumb_clients_are_alive ()
{
    for (const auto & c : s_client_list)
    {
        if (c->is_dumb_client() && c->pid() > 0)
        {
            util::info_message("Waiting for", V(c->name_with_id()));
            return true;
        }
    }
    return false;
}

void
wait_for_dumb_clients_to_die ()
{

    gui_msg("Waiting for dumb clients to die...");
    for (int i = 0; i < 6; ++i)
    {
        if (dumb_clients_are_alive())
        {
            handle_child_signal();
            usleep(50000);
        }
        else
            break;
    }
    gui_msg("Done waiting");

    /* FIXME: give up on remaining clients and purge them */
}

/*
 *  This replaced the Loop 1, Loop 2 ... 60 message from
 *  wait_for_dumb_clients_to_die(), where one couldn't see which client
 *  actually was hanging.
 */

bool
killed_clients_are_alive ()
{
    for (const auto & c : s_client_list)
    {
        bool quit_kill =
            c->pending_command() == nsm::command::quit ||
            c->pending_command() == nsm::command::kill
            ;

        if (quit_kill && c->pid() > 0)
        {
            util::info_message("Waiting for", V(c->name_with_id()));
            return true;
        }
    }
    return false;
}

/**
 * The clients that are still alive are dangerous to the user.  Their GUI
 * will be most likely hidden or non-responsive, their JACK client still
 * open. And now the session will close, maybe even nsmd will quit.
 * The hanging process is left open and invisible to the user. As a last
 * resort it must be killed until we lose control over the process.
 */

void
wait_for_killed_clients_to_die ()
{
    const int timeout = 10;             /* instead of 30                    */
    util::info_printf("Waiting %d seconds for killed clients to die", timeout);
    for (int i = 0; i < timeout; ++i)
    {
        if (! killed_clients_are_alive())
        {
            util::info_message("All clients have died.");
            return;
        }
        handle_child_signal();
        purge_dead_clients();
        s_osc_server->check();          /* check OSC for /progress messages */
        sleep(1);
    }

    util::warn_message("Killed clients are still alive");
    for (const auto & c : s_client_list)
    {
        if (c->pid() > 0 )
        {
            util::warn_message("SIGKILL to", V(c->name_with_id()));
            kill(c->pid(), SIGKILL);
        }
    }
    return;
}

void
command_all_clients_to_save ()
{
    if (! s_session_path.empty())
    {
        gui_msg("Commanding attached clients to save");

        int save_error = save_session_file();
        if (save_error == 1)
        {
            gui_msg
            (
                "The session file is write-protected; "
                "will not forward save command to clients"
            );
            util::warn_message
            (
                "Aborting client save commands; the session "
                "file is write-protected"
            );
            return;
        }
        for (auto & c : s_client_list)
            command_client_to_save(c);

        wait_for_replies();
    }
}

void
command_client_to_stop (Client * c)
{
    gui_msg("Stopping client %s", CSTR(c->name_with_id()));
    if (c->pid() > 0)
    {
        c->pending_command(nsm::command::kill);
        kill(c->pid(), SIGTERM);

        c->status("stopped");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
}

void
command_client_to_quit (Client * c)
{
    util::info_message("Commanding client to quit", c->name_with_id());
    if (c->active())
    {
        c->pending_command(nsm::command::quit);
        kill(c->pid(), SIGTERM);
        c->status("quit");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
    else if (c->is_dumb_client())
    {
        if (c->pid() > 0)
        {
            c->status("quit");
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());

            c->pending_command(nsm::command::quit); /* KILL?                */
            kill(c->pid(), SIGTERM);            /* dumb client...kill it    */
        }
        else
        {
            c->status("removed");
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());
        }
    }
}

void
delete_lock_file (const std::string & filename)
{
    bool ok = util::file_delete(filename);
    if (ok)
    {
        util::info_message("Deleted lock file", filename);
    }
    else
    {
        int ec = errno;
        util::error_printf
        (
            "Failed to delete lock file %s; error: %s",
            V(filename), std::strerror(ec)
        );
    }
}

/*
 *  Empty string = no current session.
 */

void
close_session ()
{
    if (! s_session_path.empty())
    {
        for (auto & c : s_client_list)
            command_client_to_quit(c);

        wait_for_killed_clients_to_die();
        purge_inactive_clients();
        clear_clients();

        std::string sessionlock = nsm::get_lock_file_name
        (
            s_lockfile_directory, s_session_name, s_session_path
        );
        delete_lock_file(sessionlock);
        util::info_message("Session closed", s_session_path);
        s_session_path.clear();
        s_session_name.clear();
        gui_send("/nsm/gui/session/name", "", "");
    }
}

void
tell_client_session_is_loaded (Client * c)
{
    if (c->active())                    /* && ! c->is_dumb_client()    */
    {
        util::info_printf
        (
            "Telling client %s that session is loaded",
            CSTR(c->name_with_id())
        );
        s_osc_server->send(c->addr(), "/nsm/client/session_is_loaded");
    }
}

void
tell_all_clients_session_is_loaded ()
{
    util::info_message("Telling all clients that session is loaded...");
    for (const auto & c : s_client_list)
        tell_client_session_is_loaded(c);
}

client_list
parse_session_file (const std::string & sessionfile)
{
    client_list result;
    nsm::session_triplets lineitems = nsm::parse_session_lines(sessionfile);
    bool ok = ! lineitems.empty();
    if (ok)
    {
        for (const auto & lineitem : lineitems)
        {
            Client * c = new (std::nothrow) Client
            (
                lineitem.st_client_name,
                lineitem.st_client_exe,
                lineitem.st_client_id
            );
            if (not_nullptr(c))
            {
                std::string namewithid = util::string_asprintf
                (
                    "%s.%s",
                    V(lineitem.st_client_name), V(lineitem.st_client_id)
                );
                c->name_with_id(namewithid);
                result.push_back(c);
            }
        }
    }
    return result;
}

/*
 *  Parameter "path" is the absolute path to the session including the session
 *  root, without session.nsm. First check if the session file actually
 *  exists, before closing the current one.
 */

int
load_session_file (const std::string & path)
{
    bool havepath = ! s_session_path.empty();
    bool havename = ! s_session_name.empty();
    std::string relativepath = path.substr(s_session_root.length() + 1);
    util::info_message("Loading session", path);
    if (! session_already_exists(relativepath))
    {
        util::warn_message
        (
            "Request to load non-existent session", path
        );
        return nsm::error::no_such_file;
    }
    if (havepath && havename)
    {
        /*
         * Already in a session. This is a switch, or load during duplicate etc.
         */

        util::info_printf
        (
            "Instructed to load %s while %s still open; this is normal. "
            "Trying to switch clients intelligently, if they support it. "
            "Otherwise, closing and re-opening.",
            V(path), V(s_session_path)
        );
        std::string sessionlock = nsm::get_lock_file_name
        (
            s_lockfile_directory, s_session_name, s_session_path
        );
        delete_lock_file(sessionlock);
    }
    set_name(path);     /* simple name name for lockfiles and log messages  */

    std::string sessionfile = util::string_asprintf("%s/session.nsm", V(path));

    /*
     * Check if the lockfile already exists, which means another nsmd
     * currently has loaded the session we want to load.
     */

    std::string sessionlock = nsm::get_lock_file_name
    (
        s_lockfile_directory, s_session_name, path
    );
    if (util::file_exists(sessionlock))
    {
        util::warn_printf
        (
            "Session %s already loaded and locked by file %s",
            V(s_session_name), V(sessionlock)
        );
        return nsm::error::session_locked;
    }

    client_list newclients = parse_session_file(sessionfile);
    if (newclients.empty())
        return nsm::error::create_failed;
    else
        s_session_path = path;

    util::info_message("Commanding unneeded/dumb clients to quit");

    /*
     * Count how many instances of each client are needed in the new session.
     * The client_map is a map of integer counts keyed by the client name.
     */

    client_map clmap;
    for (auto & nc : newclients)
    {
        if (clmap.find(nc->name()) != clmap.end())
            clmap[nc->name()]++;
        else
            clmap[nc->name()] = 1;
    }

    for (auto & c : s_client_list)
    {
        bool switchable = c->is_capable_of(":switch:");
        bool found = clmap.find(c->name()) != clmap.end();
        if (! switchable || ! found)
        {
            /*
             * Client is not capable of :switch:, or is not wanted in the
             * new session.
             */

            command_client_to_quit(c);
        }
        else
        {
            /*
             * Is client is switch capable and wanted in the new session?
             * If not, we already have as many as we need, stop this one.
             */

            if (clmap[c->name()]-- <= 0)
                command_client_to_quit(c);
        }
    }

    /*
     * wait_for_replies()
     * wait_for_dumb_clients_to_die()
     */

    wait_for_killed_clients_to_die();
    purge_inactive_clients();
    for (auto & c : s_client_list)
        c->pre_existing(true);

    /*
     * In a duplicated session, clients will have the same IDs, so be sure to
     * pick the right one to avoid race conditions in JACK name registration.
     */

    util::info_message("Commanding smart clients to switch");
    for (auto & nc : newclients)
    {
        Client * c = get_client_by_name_and_id
        (
            s_client_list, nc->name(), nc->client_id()
        );
        if (is_nullptr(c))
            c = client_by_name(nc->name(), s_client_list);

        /*
         * Since we already shut down clients not capable of ':switch:', we
         * can assume that these are capable. Otherwise, sleep a little bit
         * because liblo derives its sequence of port numbers from the system
         * time (second resolution), and if too many clients start at once
         * they won't be able to find a free port.
         */

        if (not_nullptr(c) && c->pre_existing() && ! c->reply_pending())
        {
            command_client_to_switch(c, nc->client_id());
        }
        else
        {
            usleep(100 * 1000);
            launch(nc->exe_path(), nc->client_id());
        }
    }

    /*
     * This part is a little tricky... the clients need some time to send
     * their 'announce' messages before we send them 'open' and know that
     * a reply is pending, so continue waiting until they finish.
     * wait_for_replies() must check for OSC messages immediately, even if
     * no replies seem to be pending yet. Dumb clients will never send an
     * 'announce message', so we need to give up waiting on them fairly soon.
     */

    wait_for_announce();
    wait_for_replies();
    tell_all_clients_session_is_loaded();

    /*
     * We already checked if the logfile exists above, and it didn't. We also
     * tested for write permissions to our XDG run-dir, confirmed to exist.
     * We can create the lockfile now.
     */

    nsm::write_lock_file(sessionlock, s_session_path, s_osc_server->url());
    util::info_message("Session was loaded", s_session_path);
    newclients.clear();
    if (s_gui_is_active)
    {
        /*
         * This is not the case when --load-session was used. GUI announce
         * will come later.  Send two parameters to signal that the session
         * was loaded: simple session-name, relative session path below
         * session root.
         */

        std::string relativepath = path.substr(s_session_root.length() + 1);
        util::info_printf
        (
            "Informing GUI: session %s, relative path %s",
            V(s_session_name), V(relativepath)
        );
        gui_send("/nsm/gui/session/name", s_session_name, relativepath);
    }
    return nsm::error::ok;
}

OSC_HANDLER( save )
{
    (void) types; (void) argc; (void) argv; (void) user_data;
    if (s_pending_operation != nsm::command::none)
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    if (s_session_path.empty())
    {
        error_send_ex
        (
            msg, path, nsm::error::no_session_open, "No session to save"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    command_all_clients_to_save();
    reply_send_ex(msg, path, "Saved");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

OSC_HANDLER( duplicate )
{
    (void) types; (void) user_data;             /* hide unused parameters   */
    if (argc < 1)
        return (-1);

    if (s_pending_operation != nsm::command::none)
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    s_pending_operation = nsm::command::duplicate;
    if (s_session_path.empty())
    {
        error_send_ex
        (
            msg, path, nsm::error::no_session_open, "No session to save"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    if (! path_is_valid(&argv[0]->s))
    {
        error_send_ex
        (
            msg, path, nsm::error::create_failed, "Invalid session name"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    if (session_already_exists(&argv[0]->s))
    {
        error_send_ex
        (
            msg, path, nsm::error::create_failed, "Session name already exists"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    command_all_clients_to_save();
    if ( clients_have_errors() )
    {
        error_send_ex
        (
            msg, path, nsm::error::general, "Some clients could not save"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }

    std::string spath = util::string_asprintf
    (
        "%s/%s", V(s_session_root), &argv[0]->s
    );
    (void) nsm::mkpath(spath, false);

    /* FIXME:
     *
     *  Code a recursive copy instead of calling the shell.
     */

#if defined USE_OLD_CODE
    std::string cmd = util::string_asprintf
    (
        "cp -R \"%s\" \"%s\"", V(s_session_path), V(spath)
    );
    system(CSTR(cmd));
#else
    bool ok = util::fts_copy_directory(s_session_path, spath);
    if (ok)
        ok = util::file_is_directory(spath);

    if (! ok)
    {
        util::error_printf
        (
            "Could not copy %s to %s", V(s_session_path), V(spath)
        );
    }
#endif
    if (s_gui_is_active)        // && ok  [TODO]
    {
        s_osc_server->send
        (
            s_gui_address, "/nsm/gui/session/session", &argv[0]->s
        );
    }
    util::info_message("Attempting to open during DUPLICATE", spath);

    /*
     * The original session is still open. load_session_file() will close it,
     * and possibly ::switch::
     */

    if (load_session_file(spath) == nsm::error::ok)
    {
        reply_send_ex(msg, path, "Loaded");
    }
    else
    {
        error_send_ex
        (
            msg, path, nsm::error::no_such_file, "No such file"
        );
        s_pending_operation = nsm::command::none;
        return (-1);
    }
    reply_send_ex(msg, path, "Duplicated");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

/*
 *  We cannot use "new" in C++; see the osc::tag enumeration.
 */

OSC_HANDLER( newsrv )
{
    (void) types; (void) user_data;             /* hide unused parameters   */
    if (argc < 1)
        return (-1);

    if (s_pending_operation != nsm::command::none)
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    s_pending_operation = nsm::command::new_session;
    if (! path_is_valid(&argv[0]->s))
    {
        error_send_ex
        (
            msg, path, nsm::error::create_failed, "Invalid session name"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    if (session_already_exists(&argv[0]->s))
    {
        error_send_ex
        (
            msg, path, nsm::error::create_failed, "Session name already exists"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    if (! s_session_path.empty())   /* Already a session running?  */
    {
        command_all_clients_to_save();
        close_session();
    }
    gui_msg("Creating new session \"%s\"", &argv[0]->s);

    std::string spath = util::string_asprintf
    (
        "%s/%s", V(s_session_root), &argv[0]->s
    );
    if (! nsm::mkpath(spath, true))
    {
        error_send_ex
        (
            msg, path, nsm::error::create_failed, "Could not create session directory"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    s_session_path = spath;
    set_name(s_session_path);

    std::string sessionlock = nsm::get_lock_file_name
    (
        s_lockfile_directory, s_session_name, s_session_path
    );
    nsm::write_lock_file(sessionlock, s_session_path, s_osc_server->url());
    reply_send_ex(msg, path, "Created." );

    if (s_gui_is_active)
    {
        gui_send("/nsm/gui/session/session", &argv[0]->s, "");

        /*
         * Send two parameters to signal that the session was loaded:
         * simple session-name, relative session path below session root.
         */

        std::string relativepath = path;
        relativepath = relativepath.substr(s_session_root.length() + 1);
        util::info_printf
        (
            "Informing GUI of session %s, relative path %s",
            V(s_session_name), V(relativepath)
        );
        gui_send("/nsm/gui/session/name", s_session_name, relativepath);
    }
    save_session_file();
    reply_send_ex(msg, path, "Session created");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

/*
 *  The argument compar() specifies a user-defined function which may be used to
 *  order the traversal of the hierarchy.  It takes two pointers to pointers to
 *  FTSENT structures as arguments and should return a negative value, zero, or a
 *  positive value to indicate if the file referenced by its first argument comes
 *  before, in any order with respect to, or after, the file referenced by its
 *  second argument. The fts_accpath, fts_path, and fts_pathlen fields of the
 *  FTSENT structures may never be used in this comparison. If the fts_info field
 *  is set to FTS_NS or FTS_NSOK, the fts_statp field may not either.  If the
 *  compare() argument is NULL, the directory traversal order is in the order
 *  listed in path_argv for the root paths, and in the order listed in the
 *  directory for everything else.
 */

int
fts_comparer_to_process_files_before_dirs
(
    const FTSENT ** first,
    const FTSENT ** second
)
{
    if ((*first)->fts_info & FTS_F)
        return (-1);                                /* first    */
    else if ((*second)->fts_info & FTS_F)
        return 1;                                 /* last     */
    else
        return strcmp((*first)->fts_name, (*second)->fts_name);
}

static lo_address s_list_response_address;

/*
 *  Parse the s_session_root recursively for session.nsm files and send
 *  names with "/nsm/server/list".
 *
 *  Sessions can be structured with sub-directories. The file session.nsm
 *  marks a real session and is a 'leaf' of the session tree.
 *
 *  No other sessions are allowed below a dir containing session.nsm.
 *
 *  We use fts to walk the s_session_root, plus an array of paths to
 *  traverse. Each path must be null terminated and the list must end
 *  with a NULL pointer.
 *
 *  fts():
 *
 *      Two  structures (and associated types) are defined in <fts.h>.
 *      The first type is FTS, a structure representing the file hierarchy.
 *      itself. The second type is FTSENT,the structure representing a file
 *      in the hierarchy. Normally, an FTSENT structure is returned for
 *      every file in the file hierarchy.
 *
 *      -   1st parameter: The list of paths to traverse.
 *      -   2nd parameter: An options parameter. Must include either
 *          FTS_PHYSICAL or FTS_LOGICAL---they change how symbolic links
 *          are handled.
 *      -   3rd parameter is a comparator which you can optionally provide
 *          to change the traversal of the filesystem hierarchy.
 *
 *      Our comparator processes files before directories; we depend on that
 *      to remember if we are already in a session-directory.
 */

OSC_HANDLER( list )
{
    (void) argc; (void) argv; (void) types; (void) user_data;
    gui_msg("Listing sessions");
    s_list_response_address = lo_message_get_source(msg);

    char * const paths [] = { STR(s_session_root), nullptr };
    FTS * ftsp = fts_open
    (
        paths, FTS_LOGICAL,
        fts_comparer_to_process_files_before_dirs       /* defined above    */
    );
    if (ftsp == NULL)
    {
        util::error_message("fts_open() failed");
        exit(EXIT_FAILURE);
    }

    /*
     * The loop will call fts_read() enough times to get each file.
     */

    FTSENT * currentsession = NULL;
    for (;;)
    {
        FTSENT * ent = fts_read(ftsp); /* get next item; file or directory  */
        if (ent == NULL)
        {
            if (errno == 0)                 /* no more items, done  */
            {
                break;
            }
            else
            {
                util::error_message("fts_read() failed");
                exit(EXIT_FAILURE);
            }
        }

        /*
         * Handle Types of Files. Given an entry, determine if it is a file
         * or a directory. The FTS_D bit == "entering a directory". The
         * FTS_DP == "leaving a directory". FTS_F == "a file".
         * FTS_SKIP means "skip descendants of this directory".
         */

        if (ent->fts_info & FTS_D)
        {
            if (currentsession != NULL)
            {
                /*
                 * Setup that no descendants of this file are visited.
                 */

                int err = fts_set(ftsp, ent, FTS_SKIP);
                if (err != 0)
                {
                    util::error_message("fts_set() failed");
                    exit(EXIT_FAILURE);
                }
            }
        }
        else if (ent->fts_info & FTS_DP)
        {
            if (ent == currentsession)
                currentsession = NULL;
        }
        else if (ent->fts_info & FTS_F)
        {
            /*
             * basename(3)
             */

            if (util::strcompare("session.nsm", basename(ent->fts_path)))
            {
                std::string s = ent->fts_path;          /* session path     */

                /*
                 * This code starts at the end of the session-root par of
                 * the file-name and copies the rest of the root to the
                 * beginning.
                 *
                 *      memmove
                 *      (
                 *          s, s + strlen( s_session_root ) + 1,
                 *          (strlen( s ) - strlen( s_session_root )) + 1
                 *      );
                 *
                 * It basically gets the relative path.
                 */

                std::string rest = path;
                rest = rest.substr(s_session_root.length() + 1);
                s_osc_server->send
                (
                    s_list_response_address, "/reply", "/nsm/server/list",
                    rest
                );

                /*
                 * Save the directory entry. not the session.nsm entry.
                 */

                currentsession = ent->fts_parent;
            }
        }
    }

    /*
     * Close fts and check for error from the closing.
     */

    if (fts_close(ftsp) == (-1))
        util::error_message("fts_close() failed");

    /*
     * As a marker that all sessions were sent, reply with an empty string,
     * which is impossible to conflict with a session name.
     */

    s_osc_server->send
    (
        s_list_response_address, "/reply", "/nsm/server/list", ""
    );
    return osc::osc_msg_handled();
}

OSC_HANDLER( open )
{
    (void) types; (void) user_data;             /* hide unused parameters   */
    if (argc < 1)
        return (-1);

    gui_msg("Opening session %s", &argv[0]->s);
    if ( s_pending_operation != nsm::command::none )
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    s_pending_operation = nsm::command::open;
    if (! s_session_path.empty())
    {
        command_all_clients_to_save();
        if (clients_have_errors())
        {
            error_send_ex
            (
                msg, path, nsm::error::general, "Some clients could not save"
            );
            s_pending_operation = nsm::command::none;
            return osc::osc_msg_handled();
        }
    }

    std::string spath = util::string_asprintf
    (
        "%s/%s", V(s_session_root), &argv[0]->s
    );
    util::info_message("Attempting to open", spath);

    int err = load_session_file(spath);
    if (err == nsm::error::ok)
    {
        reply_send_ex(msg, path, "Loaded");
    }
    else
    {
        const char * m;
        switch (err)
        {
            case nsm::error::create_failed:

                m = "Could not create session file";
                break;

            case nsm::error::session_locked:

                m = "Session is locked by another process";
                break;

            case nsm::error::no_such_file:

                m = "The named session does not exist";
                break;

            default:
                m = "Unknown error";
                break;
        }
        error_send_ex(msg, path, err, m);
    }
    util::info_message("Done");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

OSC_HANDLER( quit )
{
    (void) path; (void) types; (void) argv;
    (void) argc; (void) msg; (void) user_data;
    close_session();
    handle_signal_clean_exit(0);
    return osc::osc_msg_handled();
}

OSC_HANDLER( abort )
{
    (void) argc; (void) argv; (void) types; (void) user_data;
    if (s_pending_operation != nsm::command::none)
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    s_pending_operation = nsm::command::close;
    if (s_session_path.empty())
    {
        error_send_ex
        (
            msg, path, nsm::error::no_session_open, "No session to abort"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }

    gui_msg("Commanding clients to quit");
    close_session();
    reply_send_ex(msg, path, "Aborted");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

OSC_HANDLER( close )
{
    (void) argc; (void) argv; (void) types; (void) user_data;
    if (s_pending_operation != nsm::command::none)
    {
        error_send_ex
        (
            msg, path, nsm::error::operation_pending, "An operation pending"
        );
        return osc::osc_msg_handled();
    }
    s_pending_operation = nsm::command::close;
    if (s_session_path.empty())
    {
        error_send_ex
        (
            msg, path, nsm::error::no_session_open, "No session to close"
        );
        s_pending_operation = nsm::command::none;
        return osc::osc_msg_handled();
    }
    command_all_clients_to_save();
    gui_msg("Commanding clients to close");
    close_session();
    reply_send_ex(msg, path, "Closed");
    s_pending_operation = nsm::command::none;
    return osc::osc_msg_handled();
}

/*
 *  Don't allow clients to broadcast NSM commands.
 */

OSC_HANDLER( broadcast )
{
    (void) path; (void) types; (void) user_data;
    if (argc < 1)
        return (-1);

    const std::string to_path = &argv[0]->s;
    const std::string nsm_path = "/nsm/";
    if (! util::strncompare(to_path, nsm_path))     /* length predetermined */
        return osc::osc_msg_handled();

    osc::osc_value_list new_args;
    for (int i = 1; i < argc; ++i)
    {
        switch (types[i])
        {
            case 's':

                new_args.push_back(osc::osc_string(&argv[i]->s));
                break;

            case 'i':

                new_args.push_back(osc::osc_int(argv[i]->i));
                break;

            case 'f':

                new_args.push_back(osc::osc_float(argv[i]->f));
                break;
        }
    }

    std::string sender_url = lo_address_get_url(lo_message_get_source(msg));
    for (const auto & c : s_client_list)
    {
        if (is_nullptr(c->addr()))
            continue;

        std::string url = lo_address_get_url(c->addr());
        if (util::strcompare(sender_url, url))
            s_osc_server->send(c->addr(), to_path, new_args);

    }

    /*
     * Also relay to attached GUI so that the broadcast can be
     * propagated to another NSMD instance.
     */

    if (s_gui_is_active)
    {
        std::string u1 = lo_address_get_url(s_gui_address);
        if (util::strcompare(u1, sender_url))
        {
            new_args.push_front(osc::osc_string(to_path));
            s_osc_server->send(s_gui_address, path, new_args);
        }
    }
    return osc::osc_msg_handled();
}

/*--------------------------------------------------------------------------
 * Client Informational Messages
 *--------------------------------------------------------------------------*/

OSC_HANDLER( progress )
{
    (void) path; (void) types; (void) user_data;
    if (argc < 1)
        return (-1);

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        c->progress(argv[0]->f);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/progress",
                CSTR(c->client_id()), c->progress()
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( is_dirty )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    util::info_message("Client sends dirty");
    if (not_nullptr(c))
    {
        c->dirty(true);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/dirty",
                CSTR(c->client_id()), c->dirty()
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( is_clean )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    util::info_message("Client sends clean");
    if (not_nullptr(c))
    {
        c->dirty(false);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/dirty",
                CSTR(c->client_id()), c->dirty()
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( gui_is_hidden )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    util::info_message("Client sends gui hidden");
    if (not_nullptr(c))
    {
        c->gui_visible(false);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/gui_visible",
                CSTR(c->client_id()), c->gui_visible()
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( gui_is_shown )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    util::info_message("Client sends gui shown");
    if (not_nullptr(c))
    {
        c->gui_visible(true);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/gui_visible",
                CSTR(c->client_id()), c->gui_visible()
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( message )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/message",
                CSTR(c->client_id()), argv[0]->i, &argv[1]->s
            );
        }
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( label )
{
    (void) path; (void) types; (void) user_data;
    if (argc < 1)
        return (-1);

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (! util::strcompare(types, "s"))
            return (-1);

        c->label(&argv[0]->s);
        if (s_gui_is_active)
        {
            s_osc_server->send
            (
                s_gui_address, "/nsm/gui/client/label",
                CSTR(c->client_id()), c->label()
            );
        }
    }
    return osc::osc_msg_handled();
}

/*--------------------------------------------------------------------------
 * Response handlers
 *--------------------------------------------------------------------------*/

OSC_HANDLER( error )
{
    (void) path; (void) types; (void) user_data;
    if (argc < 3)
        return (-1);

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        int err_code = argv[1]->i;
        const std::string & message = &argv[2]->s;
        c->set_reply(err_code, message);
        util::info_printf
        (
            "Client \"%s\" replied with error: %s (%i) in %fms",
            V(c->name_with_id()), V(message), err_code,
            c->ms_since_last_command()
        );
        c->pending_command(nsm::command::none);
        c->status("error");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
    else
        util::warn_message("Error from unknown client");

    return osc::osc_msg_handled();
}

OSC_HANDLER( reply )
{
    (void) path; (void) types; (void) user_data;
    if (argc < 2)
        return (-1);

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        const std::string & message = &argv[1]->s;
        c->set_reply(nsm::error::ok, message);
        util::info_printf
        (
            "Client \"%s\" replied with: %s in %fms",
            V(c->name_with_id()), V(message), c->ms_since_last_command()
        );
        c->pending_command(nsm::command::none);
        c->status("ready");
        gui_send("/nsm/gui/client/status", c->client_id(), c->status());
    }
    else
        util::warn_message("Reply from unknown client");

    return osc::osc_msg_handled();
}

/*--------------------------------------------------------------------------
 * GUI operations
 *--------------------------------------------------------------------------*/

OSC_HANDLER( stop )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        command_client_to_stop(c);
        if (s_gui_is_active)
            s_osc_server->send(s_gui_address, "/reply", "Client stopped");
    }
    else
    {
        if (s_gui_is_active)
            s_osc_server->send(s_gui_address, "/error", -10, "No such client.");
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( remove )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (c->pid() == 0 && ! c->active())
        {
            c->status("removed");
            gui_send("/nsm/gui/client/status", c->client_id(), c->status());
            s_client_list.remove(c);
            delete c;
            if (s_gui_is_active)
                s_osc_server->send(s_gui_address, "/reply", "Client removed");
        }
    }
    else
    {
        if (s_gui_is_active)
            s_osc_server->send(s_gui_address, "/error", -10, "No such client");
    }
    return osc::osc_msg_handled();
}

OSC_HANDLER( resume )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (c->pid() == 0 && ! c->active())
        {
            if (! launch(c->exe_path(), c->client_id()))
            {
                // TODO
            }
        }
    }

    /* FIXME: return error if no such client? */

    return osc::osc_msg_handled();
}

OSC_HANDLER( client_save )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (c->active())
            command_client_to_save(c);
    }

    /* FIXME: return error if no such client? */

    return osc::osc_msg_handled();
}

OSC_HANDLER( client_show_optional_gui )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (c->active())
            s_osc_server->send(c->addr(), "/nsm/client/show_optional_gui");
    }

    /* FIXME: return error if no such client? */

    return osc::osc_msg_handled();
}

OSC_HANDLER( client_hide_optional_gui )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    Client * c = get_client_by_address(lo_message_get_source(msg));
    if (not_nullptr(c))
    {
        if (c->active())
            s_osc_server->send(c->addr(), "/nsm/client/hide_optional_gui");
    }

    /* FIXME: return error if no such client? */

    return osc::osc_msg_handled();
}

/*
 *  This is sent for a new and empty nsmd as well as already running,
 *  headless, ones. If a GUI connects to an existing server with a running
 *  session this will trigger a list of clients send to the new GUI.
 */

void
announce_gui (const std::string & url, bool is_reply)
{
    util::info_message("GUI announced from URL", V(url));
    s_gui_address = lo_address_new_from_url(CSTR(url));
    s_gui_is_active = true;
    if (is_reply)
    {
        /*
         * The default case. A GUI starts its own nsmd or connects to a
         * running one.
         */

        s_osc_server->send(s_gui_address, "/nsm/gui/gui_announce", "hi");
    }
    else
    {
        /*
         * The server was started directly and instructed to connect to a
         * running GUI.
         */

        s_osc_server->send(s_gui_address, "/nsm/gui/server_announce", "hi");
    }

    /*
     * The session root is not included in /nsm/gui/session/name.
     * For the general information we need to send this message:
     */

    s_osc_server->send(s_gui_address, "/nsm/gui/session/root", s_session_root);

    /*
     * Send session name and relative path. If both are empty, it signals that
     * no session is currently open, which is the default state if a GUI
     * started nsmd.  No s_session_path without s_session_name. We only need
     * to test for s_session_name. Sending two empty strings to indicate
     * no current session.
     */

    if (s_session_name.empty())
    {
        util::info_message("Informing GUI", "No session running");
        s_osc_server->send(s_gui_address, "/nsm/gui/session/name", "", "");
    }
    else
    {
        /*
         * Send a list of clients to the newly registered GUI in case there
         * was already a session open. First clients, then session name was
         * original nsmd order. We keep it that way; the only change is that
         * we made even the attempt dependent on a running session.
         */

        util::info_printf
        (
            "Informing GUI: %li running clients",
            s_client_list.size()
        );
        for (const auto & klient : s_client_list)
        {
            Client * c = klient;
            s_osc_server->send /* we send new twice. see announce() comment */
            (
                s_gui_address, "/nsm/gui/client/new",
                CSTR(c->client_id()), CSTR(c->exe_path())
            );
            if (! c->status().empty())
            {
                s_osc_server->send
                (
                    s_gui_address, "/nsm/gui/client/status",
                    CSTR(c->client_id()), CSTR(c->status())
                );
            }
            if (c->is_capable_of(":optional-gui:"))
            {
                s_osc_server->send
                (
                    s_gui_address, "/nsm/gui/client/has_optional_gui",
                    CSTR(c->client_id())
                );
            }
            if (! c->label().empty())
            {
                s_osc_server->send
                (
                    s_gui_address, "/nsm/gui/client/label",
                    CSTR(c->client_id()), CSTR(c->label())
                );
            }
            if (c->active())
            {
                s_osc_server->send              /* upgrade to pretty-name   */
                (
                    s_gui_address, "/nsm/gui/client/new",
                    CSTR(c->client_id()), CSTR(c->name())
                );
            }
        }

        std::string relativepath = s_session_path;
        relativepath = relativepath.substr(s_session_root.length() + 1);
        util::info_printf
        (
            "Informing GUI: session %s, relative path %s",
            V(s_session_name), V(relativepath)
        );
        s_osc_server->send
        (
            s_gui_address, "/nsm/gui/session/name",
            CSTR(s_session_name), relativepath
        );
    }
    util::info_message("Registration with GUI complete");
}

OSC_HANDLER( gui_announce )
{
    (void) path; (void) argc; (void) argv; (void) types; (void) user_data;

    announce_gui(lo_address_get_url(lo_message_get_source(msg)), true);
    return osc::osc_msg_handled();
}

OSC_HANDLER( ping )
{
    (void) msg; (void) argc; (void) argv; (void) types; (void) user_data;

    s_osc_server->send(lo_message_get_source(msg), "/reply", path);
    return osc::osc_msg_handled();
}

OSC_HANDLER( null )
{
    (void) msg; (void) argc; (void) argv; (void) user_data;

    util::warn_printf
    (
        "Unrecognized message with type signature \"%s\" at path \"%s\"",
        types, path
    );
    return osc::osc_msg_handled();
}

/*--------------------------------------------------------------------------
 * New helpers for main()
 *--------------------------------------------------------------------------*/

/*
 * Print usage message according to POSIX.1-2017
 */

void help ()
{
    static std::string usage =

"nsmd - Daemon and server for the 'New Session Manager'\n\n"
"Usage:\n"
"  nsmd [ options ]\n"
"\n"
"Options:\n"
"  --help                Show this screen.\n"
"  --version             Show version.\n"
"  --osc-port portnum    OSC port number Default: provided by system.\n"
"  --session-root path   Base path for sessions.\n"
"                        Default: $XDG_DATA_HOME/nsm/\n"
"  --load-session name   Load existing session. \"name\" is a directory\n"
"                        name in the session-root, e.g. \"My Songs\".\n"
"  --gui-url url         Connect to running NSM legacy-gui.\n"
"                        Example: osc.udp://mycomputer.localdomain:38356/.\n"
"  --detach              Detach from console.\n"
"  --quiet               Suppress messages except warnings and errors.\n"
"\n\n"
"nsmd can be run headless with existing sessions. To create new ones it\n"
"is recommended to use a GUI such as nsm-legacy-gui or Agordejo.\n"
"\n"
    ;
    puts(CSTR(usage));
}

/*
 *  Creates another sub-directory for daemons ".../nsm/d/" where each
 *  daemon has a port number file.  The actual daemon file will be written
 *  in main() after announcing the session URL.
 */

bool
make_daemon_directory ()
{
    bool result = true;
    std::string daemondirectory = util::string_asprintf
    (
        "%s/d", V(s_lockfile_directory)
    );
    result = util::make_directory_path(daemondirectory, 0771);
    if (! result)
    {
        util::error_printf
        (
            "Failed to create daemon file directory %s with error: %s",
            V(daemondirectory), std::strerror(errno)
        );
    }

    /*
     *  Reported in main().
     *
     *  s_daemon_file = util::string_asprintf
     *  (
     *      "%s/%d", V(daemondirectory), getpid()
     *  );
     *  util::info_message("Daemon file", s_daemon_file);
     */

    return result;
}

/**
 *  Adds an OSC handler function using endpoint::add_method().
 *  It first calls osc::tag_lookup() [see the messages module]
 *  to get the path + typespec pair (e.g. "/error" + "sis").
 *  It passes these two values and the other parameters to
 *  endpoint::add_method().
 *
 * \param t
 *      Provides the name of a path/typespec pair to be looked up.
 *
 * \param f
 *      Provides a function to be used as an lo_method_handler.
 *      This can be a non-member function or a static member function
 *      with the method_handler function signature [int (...)]:
 *
 *          -   int osc_reply (...)
 *          -   &endpoint::osc_reply (...)
 *
 * \param argument_description
 *      A string providing one or more parameter names, comma-separated,
 *      to indicate the expected data.
 */

void
add_method
(
    osc::tag t,
    osc::method_handler f,
    const std::string & argument_description
)
{
    std::string msg, pattern;
    if (osc::tag_lookup(t, msg, pattern))
    {
        (void) s_osc_server->add_method
        (
            msg, pattern, f, NULL, V(argument_description)
        );
    }
}

/**
 *  Issues:
 *
 *      The original nsmd uses "/nsm/gui/gui_announce" + "" because
 *      it expects the client to send just the tag message, we think.
 *      The nsm-legacy-gui sends this bare message, but it expects
 *      a response with an ID, we think.
 *
 *      We have these announces:
 *
 *          osc::tag::ctlannounce :     "/nsm/gui/server/announce" + "s"
 *          osc::tag::guiannounce :     "/nsm/gui/gui_announce" + "s"
 *          osc::tag::guisrvannounce :  "/nsm/gui/server_announce + "s"
 *          osc::tag::srvannounce :     "/nsm/server/announce" + "sssiii"
 *
 *      We add osc::tag::announce :     "/nsm/gui/gui_announce" + ""
 */

void
add_methods ()
{
    /*
     *  Already done in osc::lowrapper, the base class of osc::endpoint.
     *
     *  add_method(osc::tag::reply, OSC_NAME( reply ), "errcode,msg");
     *  add_method(osc::tag::error, OSC_NAME( error ), "errcode,msg");
     *
     *  Note that osc::tag::announce is "/nsm/gui/gui_announce" + ""
     */

    add_method(osc::tag::cliprogress, OSC_NAME( progress ), "progress");
    add_method(osc::tag::clidirty, OSC_NAME( is_dirty ), "dirtiness");
    add_method(osc::tag::cliclean, OSC_NAME( is_clean ), "dirtiness");
    add_method(osc::tag::climessage, OSC_NAME( message ), "message");
    add_method(osc::tag::guihidden, OSC_NAME( gui_is_hidden ), "message");
    add_method(osc::tag::guishown, OSC_NAME( gui_is_shown ), "message");
    add_method(osc::tag::clilabel, OSC_NAME( label ), "message");

    /*
     * Ooops: add_method(osc::tag::announce, OSC_NAME( announce ), "");
     */

    add_method(osc::tag::guiannounce, OSC_NAME( gui_announce ), "");    // s?
    add_method(osc::tag::guistop, OSC_NAME( stop ), "client_id");
    add_method(osc::tag::guiremove, OSC_NAME( remove ), "client_id");
    add_method(osc::tag::guiresume, OSC_NAME( resume ), "client_id");
    add_method(osc::tag::guisave, OSC_NAME( client_save ), "client_id");
    add_method
    (
        osc::tag::guishow, OSC_NAME( client_show_optional_gui ), "client_id"
    );
    add_method
    (
        osc::tag::guihide, OSC_NAME( client_hide_optional_gui ), "client_id"
    );
    add_method(osc::tag::oscping, OSC_NAME( ping ), "");
    add_method
    (
        osc::tag::srvannounce, OSC_NAME( announce ),
        "client_name,capabilities,executable,api_version_major,"
        "api_version_minor,client_pid"
    );
    add_method(osc::tag::srvbroadcast, OSC_NAME( broadcast ), "");
    add_method(osc::tag::srvduplicate, OSC_NAME( duplicate ), "");
    add_method(osc::tag::srvabort, OSC_NAME( abort ), "");
    add_method(osc::tag::srvlist, OSC_NAME( list ), "");
    add_method(osc::tag::srvadd, OSC_NAME( add ), "executable_name");
    add_method(osc::tag::srvnew, OSC_NAME( newsrv ), "name");   /* "new"    */
    add_method(osc::tag::srvsave, OSC_NAME( save ), "");
    add_method(osc::tag::srvopen, OSC_NAME( open ), "name");
    add_method(osc::tag::srvclose, OSC_NAME( close ), "");
    add_method(osc::tag::srvquit, OSC_NAME( quit ), "");
    add_method(osc::tag::null, OSC_NAME( null ), "");
}

/*
 * We want a clean exit even when things go wrong.
 */

void
handle_signal_clean_exit (int sig)
{
    std::string signame { "SIG ?" };
    if (sig == 0)
        signame = "None";
    else if (sig == 1)
        signame = "SIGHUP";
    else if (sig == 2)
        signame = "SIGINT";
    else if (sig == 11)
        signame = "SIGSEGV";
    else if (sig == 15)
        signame = "SIGTERM";

    util::status_printf("Handling signal %d (%s)\n", sig, V(signame));
    close_session();
    if (util::file_delete(s_daemon_file))
        util::info_message("Deleted daemon file", s_daemon_file);

    exit(0);
}

/**
 *  Handle signals. Not used: SIGQUIT; SIGHUP; SIGPIPE.
 */

void
set_traps ()
{
    std::signal(SIGHUP, handle_signal_clean_exit);
    std::signal(SIGINT, handle_signal_clean_exit);
    std::signal(SIGTERM, handle_signal_clean_exit);
    std::signal(SIGSEGV, handle_signal_clean_exit);
}

/*--------------------------------------------------------------------------
 * main()
 *--------------------------------------------------------------------------*/

int
main (int argc, char * argv [])
{
    util::set_verbose(true);
    cfg::set_client_name(NSMD66_APP_NAME);  /* the name displayed on screen */
    set_traps();
    (void) signal_descriptor();             /* initialize signal file desc. */

    /*
     * Command line parameters.
     */

    std::string osc_port;
    std::string gui_url;
    std::string load_session;
    static struct option long_options [] =
    {
        { "detach",         no_argument,        0, 'd' },
        { "session-root",   required_argument,  0, 's' },
        { "osc-port",       required_argument,  0, 'p' },
        { "gui-url",        required_argument,  0, 'g' },
        { "help",           no_argument,        0, 'h' },
        { "version",        no_argument,        0, 'v' },
        { "load-session",   required_argument,  0, 'l'},
        { "quiet",          no_argument,        0, 'q'},    /* no info msgs */
        { 0, 0, 0, 0 }
    };
    int option_index = 0;
    int c = 0;
    bool detach = false;
    while
    (
        (
            c = getopt_long_only(argc, argv, "", long_options, &option_index)
        ) != (-1)
    )
    {
        switch (c)
        {
        case 'd':

            detach = true;
            break;

        case 's':
        {
            /*
             * Get rid of trailing slash
             */

            s_session_root = std::string(optarg);
            if (s_session_root.back() == '/')
                s_session_root.erase(s_session_root.size() - 1);
            break;
        }
        case 'p':

            util::info_message("OSC port", optarg);
            osc_port = optarg;
            break;

        case 'g':

            util::info_message("Connecting to GUI at", optarg);
            gui_url = optarg;
            break;

        case 'l':

            util::info_message("Session request", optarg);
            load_session = optarg;
            break;

        case 'v':

            printf("%s " NSMD_VERSION_STRING "\n", argv[0]);
            exit(EXIT_SUCCESS);
            break;

        case 'q':

            util::set_verbose(false);
            break;

        case 'h':

            help();
            exit(EXIT_SUCCESS);
            break;
        }
    }

    /*
     * bool ok = nsm::make_xdg_runtime_lock_directory(s_session_subdir);
     */

    bool ok = nsm::make_xdg_runtime_lock_directory(s_lockfile_directory);
    if (ok)
    {
        ok = nsm::make_daemon_directory(s_lockfile_directory, s_daemon_file);
        if (ok)
        {
            if (s_session_root.empty())
                ok = nsm::make_session_root(s_session_root);    /* fill root    */
        }
    }
    if (ok)
    {
        s_osc_server = new (std::nothrow) osc::endpoint();
        ok = not_nullptr(s_osc_server);
        if (ok)
        {
            ok = s_osc_server->init(LO_UDP, osc_port, true);    /* 'this'   */

            /*
             *  Get the URL and write it into the s_daemon_file that is named
             *  after our PID.
             *
             *  Already reported in lowrapper:
             *
             *      util::status_message("NSM_URL", url);
             */

            std::string url = s_osc_server->url();
            url += "\n";
            ok = util::file_write_string(s_daemon_file, url);
            if (ok)
            {
                util::info_message("Wrote daemon file", s_daemon_file);
            }
            else
            {
                util::error_printf
                (
                    "Failed to write daemon file to %s: %s",
                    V(s_daemon_file), std::strerror(errno)
                );
            }
        }
        else
        {
            util::error_message("Failed to create OSC server, exiting" );
            exit(EXIT_FAILURE);
        }
    }
    if (! gui_url.empty())
    {
        /*
         *  The server was started directly and instructed to connect to a
         *  running GUI.
         */

        announce_gui(gui_url, false);
    }
    add_methods();                              /* response handlers        */
    if (! load_session.empty())                 /* this is a session name   */
    {
        /*
         * Build the session path. --load-session works with --session-root
         */

        std::string spath = util::string_asprintf
        (
            "%s/%s", V(s_session_root), V(load_session)
        );
        load_session_file(spath);
    }
    if (detach)
    {
        util::info_message("Detaching from console");
        if (fork())
        {
            exit(EXIT_SUCCESS);
        }
        else
        {
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);
        }
    }

    /*
     * Listen for sigchld signals and process OSC messages forever.  This
     * still has some corner cases, like a race condition on startup that
     * never gets the real PID, but we cover the majority of cases at least.
     */

    int start_ppid = getppid();                         /* get parent pid   */
    for (;;)
    {
        wait(1000);
        if (start_ppid != getppid())
        {
            util::warn_printf
            (
                "Parent PID changed from %d to %d, indicating "
                "a possible client crash. "
                "The user has no control over the session. "
                "Try to shut down cleanly.",
                int(start_ppid), int(getppid())
            );
            handle_signal_clean_exit(0);                /* 0 == no signal   */
        }
    }

    /*
     * Code after here will not be executed if nsmd is stopped with any
     * abort-signal like SIGINT. Without a signal handler clients will remain
     * active ("zombies") without nsmd as parent. Therefore exit is handled by
     * handle_signal_clean_exit().
     */

    return EXIT_SUCCESS;
}

/*
 * nsmctl.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

