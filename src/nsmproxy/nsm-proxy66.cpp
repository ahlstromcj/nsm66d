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
 * \file          nsm-proxy66.cpp
 *
 *    This module refactors the nsm-proxy application to replace C code with
 *    C++ code.
 *
 * \library       nsm-proxy66 application
 * \author        Chris Ahlstrom and other authors; see documentation
 * \date          2025-02-25
 * \updates       2025-04-16
 * \version       $Revision$
 * \license       GNU GPL v2 or above
 *
 *   To do.
 */

#include <cerrno>                       /* #include <errno.h>               */
#include <csignal>                      /* std::signal() and <signal.h>     */
#include <cstring>                      /* std::strerror()                  */
#include <cstdlib>                      /* std::getenv(), std::rand()       */
#include <getopt.h>                     /* GNU get command-line option      */
#include <sys/signalfd.h>               /* struct signalfd_siginfo          */
#include <sys/wait.h>                   /* wait() or waitpid()              */

#include "cfg/appinfo.hpp"              /* cfg66: cfg::set_client_name()    */
#include "nsm/nsmcodes.hpp"             /* nsm66: nsm::error enumeration    */
#include "nsm/nsmproxy.hpp"             /* nsm66: nsm::nsmproxy class       */
#include "nsmproxy/nsm-proxy66.hpp"     /* nsm66: nsmp-proxy66 header       */
#include "osc/lowrapper.hpp"            /* nsm66: LO_TT_IMMEDIATE_2 macro   */
#include "osc/messages.hpp"             /* cfg66: osc::tag enumeration      */
#include "util/filefunctions.hpp"       /* cfg66: util::file_write_lines()  */
#include "util/strfunctions.hpp"        /* cfg66: util::string_asprintf()   */

#define NSM_PROXY_APP_NAME              "NSM Proxy 66"
#define NSM_PROXY_APP_TITLE             "NSM Proxy 66"
#define OSC_NAME(name)                  osc_ ## name
#define NSM_PROXY_CONFIG_FILE_NAME      "nsm-proxy.config"
#define NSM_PROXY66_CLIENT_NAME         "proxy66"

static lo_server g_osc_server;
static lo_address g_nsm_lo_address;
static lo_address g_gui_address;
static int g_nsm_is_active;
static std::string g_project_file;
static int g_die_now = 0;
static int g_signal_fd;
static std::string g_nsm_client_id;
static std::string g_nsm_display_name;

nsm::nsmproxy &
nsm_proxy ()
{
    static nsm::nsmproxy s_nsm_proxy;
    return s_nsm_proxy;
}

bool
snapshot (const std::string & file)
{
    return nsm_proxy().dump(file);
}

#if 0

void
process_announce
(
    const std::string & nsm_url,
    const std::string & client_name,
    const std::string & process_name
)
{
    util::info_message("Announcing to NSM");
    lo_address to = lo_address_new_from_url(CSTR(nsm_url) );
    int pid = int(getpid());
    lo_send_from
    (
        to, g_osc_server, LO_TT_IMMEDIATE_2,
        "/nsm/server/announce", "sssiii",
        CSTR(client_name), ":optional-gui:", CSTR(process_name),
        NSM_API_VERSION_MAJOR,                      /* 1 api_major_version  */
        NSM_API_VERSION_MINOR,                      /* 0 api_minor_version  */
        pid
    );
    lo_address_free(to);
}

#endif

bool
open (const std::string & file)
{
    std::string path = util::string_asprintf
    (
        "%s/%s", V(file), NSM_PROXY_CONFIG_FILE_NAME
    );
    bool result = nsm_proxy().restore(path);
    return result;
}

/****************/
/* OSC HANDLERS */
/****************/

/* NSM */

int
osc_announce_error
(
    const char * path,
    const char * types,
    lo_arg ** argv,
    int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) msg; (void) user_data;
    if (argc >= 3)
    {
        if (std::string(types) != "sis")
            return osc::osc_msg_unhandled();

        if (std::string("/nsm/server/announce") != std::string(&argv[0]->s))
             return osc::osc_msg_unhandled();
    }
    util::error_message("Failed to register with NSM", &argv[2]->s);
    g_nsm_is_active = false;
    return osc::osc_msg_handled();
}

int
osc_announce_reply
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) user_data;
    if (argc >= 2)
    {
        if (std::string("/nsm/server/announce") != std::string(&argv[0]->s))
             return osc::osc_msg_unhandled();

        util::status_message("Successfully registered", &argv[1]->s);
        g_nsm_is_active = true;
        g_nsm_lo_address = lo_address_new_from_url
        (
            lo_address_get_url(lo_message_get_source(msg))
        );
    }
    return osc::osc_msg_handled();
}

int
osc_save
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) types; (void) argc; (void) argv; (void) msg; (void) user_data;

    bool r = snapshot(g_project_file);
    nsm_proxy().save();
    if (r)
    {
        lo_send_from
        (
            g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
            "/reply", "ss", path, "OK"
        );
    }
    else
    {
        lo_send_from
        (
            g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
            "/error", "sis", path,
            nsm::error::save_failed, "Error saving project file"
        );
    }
    return osc::osc_msg_handled();
}

static int s_gui_pid;

void
show_gui ()
{
    int pid;
    if (! (pid = fork()))
    {
        std::string executable = "nsm-proxy-gui";
        char * url = lo_server_get_url(g_osc_server);       // need to free()???
        char * args [] =
        {
            STR(executable), strdup( "--connect-to" ), url, NULL
        };
        util::info_message("Launching", executable);
        if (execvp(STR(executable), args) == (-1))
        {
            util::error_message("Error starting process", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    s_gui_pid = pid;
    lo_send_from
    (
        g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
        "/nsm/client/gui_is_shown", ""
    );
}

int
osc_show_gui
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) argc; (void) argv; (void) msg; (void) types; (void) user_data;
    show_gui();

    /* FIXME: detect errors */

    lo_send_from
    (
        g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
        "/reply", "ss", path, "OK"
    );
    return osc::osc_msg_handled();
}

void
hide_gui ()
{
    if (s_gui_pid != 0)
        kill(s_gui_pid, SIGTERM);
}

int
osc_hide_gui
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) argc; (void) argv; (void) msg; (void) types; (void) user_data;
    hide_gui();
    lo_send_from
    (
        g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
        "/nsm/client/gui_is_hidden", ""
    );

    /* FIXME: detect errors */

    lo_send_from
    (
        g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
        "/reply", "ss", path, "OK"
    );
    return osc::osc_msg_handled();
}

int
osc_open
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) types; (void) msg; (void) user_data;
    if (argc >= 3)
    {
        const std::string & new_path = &argv[0]->s;
        const std::string & display_name = &argv[1]->s;
        const std::string & client_id = &argv[2]->s;
        g_nsm_client_id = client_id;
        g_nsm_display_name = display_name;

        bool ok = util::make_directory_path(new_path, 0777);
        if (ok)
            ok = util::set_current_directory(new_path);

        std::string  new_filename = util::string_asprintf
        (
            "%s/%s", V(new_path), NSM_PROXY_CONFIG_FILE_NAME
        );
        if (util::file_exists(new_filename))
        {
            if (open(new_path))
            {
                // no code
            }
            else
            {
                lo_send_from
                (
                    g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
                    "/error", "sis", path, -1, "Could not open file"
                );
                return osc::osc_msg_handled();
            }
            lo_send_from
            (
                g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
                "/nsm/client/gui_is_hidden", ""
            );
        }
        else
            show_gui();

        g_project_file = new_path;
        lo_send_from
        (
            g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
            "/reply", "ss", path, "OK"
        );
        if (g_gui_address)
            nsm_proxy().update(g_gui_address);
    }
    return osc::osc_msg_handled();
}

/* GUI */

int
osc_label
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) msg; (void) user_data;
    if (argc >= 1)
        nsm_proxy().label(&argv[0]->s);

    return osc::osc_msg_handled();
}

int
osc_save_signal
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) msg; (void) user_data;
    if (argc >= 1)
        nsm_proxy().save_signal(argv[0]->i);

    return osc::osc_msg_handled();
}

int
osc_stop_signal
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) msg; (void) user_data;
    if (argc >= 1)
        nsm_proxy().stop_signal(argv[0]->i);

    return osc::osc_msg_handled();
}

int
osc_start
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) msg; (void) user_data;
    snapshot(g_project_file);
    if (argc >= 3)
    {
        if ( nsm_proxy().start( &argv[0]->s, &argv[1]->s, &argv[2]->s ) )
            hide_gui();
    }
    return osc::osc_msg_handled();
}

int
osc_kill
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) argc; (void) argv;
    (void) msg; (void) user_data;
    nsm_proxy().kill();
    return osc::osc_msg_handled();
}

int
osc_update
(
    const char * path,
    const char * types,
    lo_arg ** argv, int argc,
    lo_message msg,
    void * user_data
)
{
    (void) path; (void) types; (void) argc; (void) argv; (void) user_data;
    lo_address to = lo_address_new_from_url
    (
        lo_address_get_url(lo_message_get_source(msg))
    );
    nsm_proxy().update(to);
    g_gui_address = to;
    return osc::osc_msg_handled();
}

void
signal_handler (int /*x*/)
{
    /*
     * Hmmmmmmm.
     */

    g_die_now = true;
}

void
set_traps ()
{
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

void
set_signals ()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    g_signal_fd = signalfd((-1), &mask, SFD_NONBLOCK);
}

void
add_method
(
    osc::tag t,
    osc::method_handler f,
    const std::string & params
)
{
    std::string msg, pattern;
    if (osc::tag_lookup(t, msg, pattern))
    {
        lo_server_add_method
        (
            g_osc_server, CSTR(msg), OPTR(pattern), f, NULL // CSTR(params)
        );
        util::info_message("Method parameters", params);
    }
}

/**
 *  The "gui" tags have a typespec of "s" (client ID), but that is not
 *  used in nsm-proxy because its clients do not support an NSM
 *  client ID. Therefore the "cli" versions, having a typespec of "",
 *  are used.
 */

void
add_methods ()
{
    /* NSM */

    add_method(osc::tag::clisave, OSC_NAME( save ), "");
    add_method(osc::tag::cliopen, OSC_NAME( open ), "");           // "name"
    add_method(osc::tag::clishow, OSC_NAME( show_gui ), "");       // "message"
    add_method(osc::tag::clihide, OSC_NAME( hide_gui ),"");        // "message"
    add_method(osc::tag::reply, OSC_NAME( announce_reply ), "");
    add_method(osc::tag::error, OSC_NAME( announce_error ), "");

    /* GUI */

    add_method(osc::tag::proxylabel, OSC_NAME( label ), "");         // "message"
    add_method(osc::tag::proxysave, OSC_NAME( save_signal ), "");
    add_method(osc::tag::proxystop, OSC_NAME( stop_signal ), "");
    add_method(osc::tag::proxykill, OSC_NAME( kill ), "");
    add_method(osc::tag::proxystart, OSC_NAME( start ), "");
    add_method(osc::tag::proxyupdate, OSC_NAME( update ), "");
}

void
init_osc (const std::string & osc_port)
{
    char * p = osc_port.empty() ? NULL : STR(osc_port) ;
    g_osc_server = lo_server_new(p, NULL);

    char * url = lo_server_get_url(g_osc_server);
    util::info_message("OSC server URL", url);
    free(url);
}

void
die ()
{
    if (s_gui_pid != 0)
    {
        util::info_message("Killing GUI");
        ::kill(s_gui_pid, SIGTERM);
    }
    nsm_proxy().kill();
    exit(EXIT_SUCCESS);
}

void
handle_sigchld ()
{
    for (;;)
    {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;

        if (pid == s_gui_pid)
        {
            lo_send_from
            (
                g_nsm_lo_address, g_osc_server, LO_TT_IMMEDIATE_2,
                "/nsm/client/gui_is_hidden", ""
            );
            s_gui_pid = 0;
            continue;                           /* we don't care...         */
        }

        if (WIFSIGNALED(status))                /* process killed w/signal  */
        {
            if
            (
                WTERMSIG(status) == SIGTERM || WTERMSIG(status) == SIGHUP ||
                WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGKILL
            )
            {
                /*
                 * process was killed via an appropriate signal
                 */

                util::info_message("child was killed (maybe by us)");
                g_die_now = true;
                continue;
            }
        }
        else if (WIFEXITED(status))
        {
            /* child called exit() or returned from main() */

            util::info_printf("child exit status: %i", WEXITSTATUS(status));
            if (WEXITSTATUS(status) == 0)
            {
                util::info_message("child exited without error");
                g_die_now = true;
                continue;
            }
            else
            {
                util::warn_message("child exited abnormally");
                nsm_proxy().handle_client_death(WEXITSTATUS(status));
            }
        }
    }
}

void
help ()
{
    static std::string usage =
    "nsm-proxy - Wrapper for executables without direct NSM-Support.\n\n"
    "It is a module for the 'New Session Manager' and only communicates\n"
    "over OSC in an NSM-Session and has no standalone functionality.\n"
    "\n"
    "Usage:\n"
    "  nsm-proxy --help\n"
    "\n"
    "Options:\n"
    "  --help                Show this screen\n"
    "\n"
    ;
    puts(CSTR(usage));
}

int
main ( int argc, char **argv )
{
    cfg::set_client_name(NSM_PROXY66_CLIENT_NAME);  /* display in messages  */
    set_traps();
    set_signals();

    static struct option long_options [] =
    {
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };
    int option_index = 0;
    int c = 0;
    while
    (
        (c = getopt_long_only(argc, argv, "", long_options, &option_index))
            != (-1)
    )
    {
        switch (c)
        {
            case 'h':

                help();
                exit(EXIT_SUCCESS);
                break;
        }
    }
    init_osc(NULL);

    /*
     * TODO: lookup the URL if necessary.
     */

    const char * nsm_url = std::getenv("NSM_URL");
    if (not_nullptr(nsm_url))
    {
        osc::process_announce
        (
            g_osc_server, ":optional-gui:", std::string(nsm_url),
            NSM_PROXY_APP_TITLE, argv[0]
        );
    }
    else
    {
        util::error_message("Could not register as NSM client");
        exit(EXIT_FAILURE);
    }

    struct signalfd_siginfo fdsi;

    /*
     * listen for sigchld signals and process OSC messages forever
     */

    for (;;)
    {
        ssize_t s = read(g_signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
        if (s == sizeof(struct signalfd_siginfo))
        {
            if (fdsi.ssi_signo == SIGCHLD)
                handle_sigchld();
        }
        lo_server_recv_noblock(g_osc_server, 500);
        if (g_die_now)
            die();
    }
}

/*
 * nsm-proxy66.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

