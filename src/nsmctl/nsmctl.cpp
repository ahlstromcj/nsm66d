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
 * \file          nsmctl.cpp
 *
 *    This module refactors the nsm-legacy-gui application to work as
 *    a command-line sender of messages to nsmd.
 *
 * \library       nsmctl application
 * \author        Chris Ahlstrom and other authors; see documentation
 * \date          2025-03-21
 * \updates       2025-04-20
 * \version       $Revision$
 * \license       GNU GPL v2 or above
 *
 *  How does nsm-legacy-gui work? In this description, we will elide the
 *  GUI elements and focus on setup and messages.
 *
 * nsm::daemon class
 *
 *      This structure specifies the URL, lo_address, and an is-child flag.
 *      It is created, and added to a list of daemons, under these various
 *      circumstances:
 *
 *          1.  In main() if the user specified a running nsmd URL such
 *              as "osc.udp://host.localdomain.3455/".
 *          2.  In main() if NSM_URL is defined in the environment.
 *          3.  In osc_handler() when "/nsm/gui/server_announce" is
 *              received. Then an "/nsm/server/list" is sent to the
 *              daemon's address.
 *
 *      In main(), a controller->announce call is made for each daemon.
 *
 *      The list of Daemons is used...
 *
 *          1.  ping(). Sends an "/osc/ping" message to each daemon.
 *          2.  cb_button().
 *
 *              a.  dirty. Send "/nsm/gui/client/save" + client ID.
 *              b.  gui. Send "/nsm/gui/client/show|hide_optional_gui" +
 *                  client ID.
 *              c.  remove. Send "/nsm/gui/client/remove" + client ID.
 *              d.  restart. Send "/nsm/gui/client/resume" + client ID.
 *              e.  kill. Send "/nsm/gui/client/stop" + client ID.
 *
 *          3.  cb_handle(). [renamed to send_server/client_message().]
 *
 *              a.  abort. Send "/nsm/server/abort".
 *              b.  close. Send "/nsm/server/close".
 *              c.  save. Send "/nsm/server/save".
 *              d.  open. Opens a session by name.
 *              e.  duplicate. Starts a new session, sending
 *                  "/nsm/server/duplicate" + name.
 *              f.  quit.
 *              g.  refresh. Send "/nsm/server/list".
 *              h.  Session browser. The user picks a name and
 *                  "/nsm/server/open" + name is sent.
 *              i.  new. Send "/nsm/server/new" + name of session.
 *              j.  add. Select a server and add a client to it by name of
 *                  the executable..
 *
 *          4.  osc_broadcast_handler(). Relays a broadcast.
 */

#include <cerrno>                       /* #include <errno.h>               */
#include <csignal>                      /* std::signal() and <signal.h>     */
#include <cstdio>                       /* std::getchar()                   */
#include <cstring>                      /* std::strerror()                  */
#include <getopt.h>                     /* getopt(3) CLI parsing            */
#include <string.h>                     /* strdup(3)                        */
#include <unistd.h>                     /* execvp(3), sleep(3)              */

#include "cfg/appinfo.hpp"              /* cfg66: cfg::set_client_name()    */
#include "nsm/helpers.hpp"              /* nsm::lookup_active_nsmd_url()    */
#include "nsm/nsmcontroller.hpp"        /* nsm::nsmcontroller class         */
#include "util/filefunctions.hpp"       /* util::get_xdg_runtime_directory()*/
#include "util/ftswalker.hpp"           /* util::ftswalker class & funcs    */
#include "util/msgfunctions.hpp"        /* util::info_message() etc.        */
#include "util/strfunctions.hpp"        /* CSTR() macro                     */

#define NSMCTL_APP_NAME         "NSM Control"
#define NSMCTL_EXE_NAME         "nsmd66"
#define NSMCTL_APP_TITLE        "NSM Control CLI"
#define NSMCTL_CAPABILITIES     ""

namespace   // anonymous
{

/**
 *  Command-line options and global status.
 */

bool s_do_env_nsm_url { true };         /* first we try NSM_URL             */
bool s_do_lookup { false };             /* the fallback if the above fails  */
bool s_do_monitor { false };            /* stay in the app until Ctrl-C     */
bool s_do_ping { false };
bool s_do_stop { false };
bool s_is_client_action { false };
bool s_die_now { false };
int s_optind { (-1) };
int s_nsmd_child_pid { 0 };
std::string s_subject_name;
std::string s_nsm_url;
std::string s_nsmd_path { "nsmd" };
osc::tag s_action_tag { osc::tag::illegal };

/*
 *  Prints usage message according to POSIX.1-2017.
 */

void help_actions ();
bool remove_xdg_run_time_directory ();

void
help ()
{
    static std::string s_usage
    {
"nsmctl - Simple command-line controller for the 'New Session Manager'\n\n"
"Usage:   nsmctl [ options | -h | --help ]\n"
"\n"
"Options:\n"
"\n"
"   -l, --lookup          Try to find a running nsmd and use its URL.\n"
"                         This is done if no NSM_URL is defined in the\n"
"                         environment and no --url is provided.\n"
"   -u, --url url         Connect to an nsmd running at a user-specified URL.\n"
"                         Example: osc.udp://mycomputer.localdomain:38356/\n"
"   -n, --nsmd-path path  Path to the nsmd application. Default is \"nsmd\".\n"
"                         \"build\" loads the executable in ./build/src/nsmctl.\n"
"   -p, --ping            Ping the server a few times.\n"
"   -m, --monitor         Keep nsmctl running in order to monitor activity.\n"
"   -q, --quiet           Turn off verbose output.\n"
"   -s, --stop            At nsmctl exit, also tell nsmd 'servers' to stop.\n"
"   -a, --action item     Run one action before exiting. See the list below.\n"
"                         If it is a client, the format of item is\n"
"                         'action@exe'; the client name or ID is required.\n"
"                         Otherwise, it is just the action name alone.\n"
"   -c, --clean           Remove the nsm run-time directory. Useful when files\n"
"                         are left from aborted actions. But BE CAREFUL!\n"
"   -i, --investigate     Enables extra output for trouble-shooting.\n"
"   --                    Everything after the -- is given to nsmd as server\n"
"                         options. See nsmd --help. In particular, one can\n"
"                         cut down the nsmd 'noise' using 'nsmctl -- --quiet.\n"
"\n"
"This program performs some of the functions of the non-session-manager\n"
"user-interface (nsm-legacy-gui), but from the command line.\n"
    };
    puts(CSTR(s_usage));
    help_actions();
}

/**
 *  Show the list of available actions that apply to the client or the
 *  server. The server actions mostly deal with sessions.
 */

void
help_actions ()
{
    lib66::tokenization actions;
    osc::tag_name_action_list(actions);
    if (! actions.empty())
    {
        std::string output
        {
            "Client/server actions:\n\n"
        };
        for (const auto & a : actions)
        {
            output += "    ";
            output += a;
            output += "\n";
        }
        output += "\n";
        output +=
"Each client action needs the name of an executable, such as 'qseq66'.\n"
"The server actions 'open', 'duplicate', & 'new' need a session name.\n"
        ;
        puts(CSTR(output));
    }
}

/**
 *  Extracts the action name, and, if present, the subject name for
 *  the action. The format of the action item is:
 *
 *      actionname[@subjectname]
 *
 *  where the part in brackets is optional for some actions and
 *  required for others.
 */

bool
parse_action_item (const std::string & item)
{
    lib66::tokenization t = util::tokenize(item, "@");
    if (t.size() > 0)
    {
        std::string msgpath;
        s_action_tag = osc::tag_name_lookup(t[0]);
        if (s_action_tag != osc::tag::illegal)
        {
            bool needed = osc::tag_needs_argument(t[0]);
            s_is_client_action = osc::tag_name_is_client(t[0]);
            msgpath = osc::tag_message(s_action_tag);
            if (needed)
            {
                if (t.size() == 2)
                {
                    s_subject_name = t[1];
                    msgpath += " ";
                    msgpath += s_subject_name;
                }
                else
                {
                    s_action_tag = osc::tag::illegal;
                    util::error_message("Subject name missing", t[0]);
                }
            }
        }
        if (s_action_tag != osc::tag::illegal)
            util::status_message("Will send", V(msgpath));
    }
    return s_action_tag != osc::tag::illegal;
}

/**
 *  Hidden external variable used by getopt():
 *
 *      optind: The index to next element in argv[]. Init'ed to 1.
 *              When getopt() returns -1, then optind is the index of
 *              the first non-option element in argv[].
 *
 *  A colon at the start initiates silent error reporting.
 *
 *  Also note that we could use cfg66's CLI parser. See
 *  cfg66/tests/cliparser_test.cpp.
 */

bool
parse_cli (int argc, char * argv [])
{
    struct option long_opts []
    {
        { "lookup",         no_argument,        0, 'l' },
        { "monitor",        no_argument,        0, 'm' },
        { "url",            required_argument,  0, 'u' },
        { "nsmd-path",      required_argument,  0, 'n' },
        { "ping",           no_argument,        0, 'p' },
        { "quiet",          no_argument,        0, 'q' },
        { "stop",           no_argument,        0, 's' },
        { "clean",          no_argument,        0, 'c' },
        { "action",         required_argument,  0, 'a' },
        { "help",           no_argument,        0, 'h' },
        { "investigate",    no_argument,        0, 'i' },
        { 0, 0, 0, 0 }
    };
    const char * const opchars = ":lmu:n:pqsca:hi";
    bool result = true;
    int optindex = 0;
    int c = 0;
    util::set_verbose(true);
    cfg::set_client_name("nsmctl");     /* the [name] displayed in output   */
    while
    (
        (c = getopt_long(argc, argv, opchars, long_opts, &optindex)) != (-1)
    )
    {
        switch (c)
        {
            case 'l':

                s_do_lookup = true;
                s_do_env_nsm_url = false;
                break;

            case 'm':

                s_do_monitor = true;
                break;

            case 'u':

                s_do_env_nsm_url = false;
                s_nsm_url = optarg;
                break;

            case 'n':

                s_nsmd_path = optarg;
                if (s_nsmd_path == "build")
                {
                    s_nsmd_path = "./build/src/nsmd/nsm66d-";
                    s_nsmd_path += NSM66D_VERSION;
                }
                break;

            case 'p':

                util::info_message("Ping option activated");
                s_do_ping = true;
                break;

            case 'q':

                util::set_verbose(false);
                break;

            case 's':

                s_do_stop = true;
                break;

            case 'c':

                result = remove_xdg_run_time_directory();
                exit(result ? EXIT_SUCCESS : EXIT_FAILURE);
                break;

            case 'a':

                if (! parse_action_item(optarg))
                {
                    util::error_message("Aborting due to bad --action");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'i':

                util::set_investigate(true);
                break;

            case 'h':

                help();
                exit(EXIT_SUCCESS);
                break;

            case ':':

                util::error_printf
                (
                    "Required value missing: %c", optopt
                );
                result = false;
                break;

            case '?':

                util::error_printf
                (
                    "Unknown option at argv[%d]: %s",
                    optind - 1, argv[optind - 1]
                );
                result = false;
                break;

            default:

                util::warn_message("Non-option", argv[optindex]);
                break;
        }
    }
    s_optind = optind;
    return result;
}

/**
 *  Clean out the run-time lock/daemon directory.
 *
 *  BEWARE!
 */

bool
remove_xdg_run_time_directory ()
{
    bool result = true;
    std::string runtimedir = util::get_xdg_runtime_directory("nsm");    // "d"
    if (! runtimedir.empty())
    {
        result = util::fts_delete_directory(runtimedir);
        if (result)
            util::status_message("Deleted", runtimedir);
        else
            util::error_message("Failed to delete", runtimedir);
    }
    return result;
}

/**
 *  A helper function for consistency.
 */

void
add_new_daemon (const std::string & nsmurl, nsm::daemon_list & alldaemons)
{
    nsm::daemon d(nsmurl, lo_address_new_from_url(CSTR(nsmurl)));
    alldaemons.push_back(d);
    util::info_message("Added to NSM daemon/connection list", nsmurl);
}

/**
 *  Provides internal "global" access to the list of all connected
 *  nsmd daemons.
 */

nsm::daemon_list &
nsm_daemon_list ()
{
    static nsm::daemon_list s_all_daemons;
    return s_all_daemons;
}

/**
 *  Provides internal "global" access to the NSM controller.
 */

nsm::nsmcontroller &
nsm_controller ()
{
    nsm::daemon_list & alldaemons = nsm_daemon_list();

#if defined USE_ORIGINAL_CODE

    static nsm::nsmcontroller s_controller(alldaemons);
    return s_controller;

#else

    std::string ctlexename { NSMCTL_EXE_NAME };
    ctlexename += "-" NSM66D_VERSION;
    static nsm::nsmcontroller s_controller
    (
        alldaemons, NSMCTL_APP_NAME, ctlexename,
        NSMCTL_CAPABILITIES, NSM_API_VERSION    /* from lowrapper.hpp       */
    );
    return s_controller;

#endif
}

/**
 *  These signals and the signal handler help for a clean exit from
 *  any nsmd child process and nsmctl itself.
 */

void
signal_handler (int sig)
{
    nsm::nsmcontroller & ctlr = nsm_controller();
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
    s_die_now = true;
    ctlr.deactivate();
    if (s_nsmd_child_pid != 0)
    {
        int rc = kill(pid_t(s_nsmd_child_pid), sig);
        if (rc == (-1))
        {
            util::error_printf
            (
                "Kill(%d) of nsmd failed: %d",
                s_nsmd_child_pid, std::strerror(errno)
            );
        }
        else
            util::info_printf("Killed nsmd, PID %d", s_nsmd_child_pid);
    }
}

void
set_traps ()
{
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGSEGV, signal_handler);
}

}           // namespace anonymous

/**
 *  The main routine. First we see if the user specified --url (basically
 *  the user is doing the lookup). If not, we try the default, using the
 *  NSM_URL environment variable if not overridden by --lookup. If
 *  that variable is not found, then --lookup is assumed.
 *
 *  Note that NSM_URL is available only if (nsmd was started at boot
 *  time?).
 */

int
main (int argc, char * argv [])
{
    util::set_verbose(true);
    cfg::set_client_name("nsmctl");     /* meaning the name displayed       */
    set_traps();
    bool ok = parse_cli(argc, argv);
    if (! ok)
        return EXIT_FAILURE;

    nsm::daemon_list & alldaemons { nsm_daemon_list() };
    nsm::nsmcontroller & ctlr { nsm_controller() };
    bool existing_server { false };
    if (! s_nsm_url.empty())            /* --url url --> s_nsm_url          */
    {
        add_new_daemon(s_nsm_url, alldaemons);
    }
    else if (s_do_env_nsm_url)          /* use NSM_URL (default operation)  */
    {
        std::string nsmurl { util::get_env("NSM_URL") };
        if (nsmurl.empty())
        {
            s_do_lookup = true;
        }
        else
        {
            s_nsm_url = nsmurl;
            add_new_daemon(nsmurl, alldaemons);
        }
    }
    if (s_do_lookup)                    /* the fallback operation           */
    {
        std::string nsmurl = nsm::lookup_active_nsmd_url();
        if (nsmurl.empty())
        {
            util::warn_message
            (
                "Lookup: No NSM URL found in /run/user/../.nsm"
            );
        }
        else
        {
            s_nsm_url = nsmurl;
            add_new_daemon(nsmurl, alldaemons);
            existing_server = true;
        }
    }

    bool inited { false };
    if (existing_server)
    {
        // std::string portnumber = osc::extract_port_number(s_nsm_url);
        // inited = ctlr.init_osc(portnumber);

        inited = ctlr.init_osc();
    }
    else
        inited = ctlr.init_osc();       /* make osc::endpoint, add methods  */

    if (inited)
    {
        if (alldaemons.empty())         /* we need to start a new daemon    */
        {
            std::string url = ctlr.url();
            if (! url.empty())
            {
                s_do_monitor = true;    /* otherwise we exit & kill nsmd    */

                /*
                 * A successful fork() returns 0 in the child, the child's
                 * PID in the parent, and -1 otherwise. The job of the child
                 * is to start nsmd, passing it any parameters after a
                 * "--" marker.
                 */

                int pid = int(fork());
                if (pid == (-1))
                {
                    util::error_message("Fork failed", std::strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (pid == 0)           /* successful fork to child process */
                {
                    /*
                     * Provide the NSM URL via nsmd's --gui-url option.
                     * Also pass non-option arguments on to the daemon.
                     */

                    char ** args = new (std::nothrow)
                        char * [4 + argc - s_optind];

                    if (not_nullptr(args))
                    {
                        std::string nsmdname = s_nsmd_path;
                        int i = 0;
                        args[i++] = strdup(STR(s_nsmd_path));
                        args[i++] = strdup("--gui-url");
                        args[i++] = STR(url);
                        nsmdname += " --gui-url ";
                        nsmdname += url;
                        for ( ; s_optind < argc; ++i, ++s_optind)
                        {
                            args[i] = argv[s_optind];
                            nsmdname += " ";
                            nsmdname = argv[s_optind];
                        }
                        args[i] = nullptr;
                        util::info_message("Starting nsmd daemon", nsmdname);
                        if (execvp(CSTR(s_nsmd_path), args) == (-1)) // nsmd
                        {
                            util::error_printf
                            (
                                "%s execvp error: %s",
                                V(s_nsmd_path), std::strerror(errno)
                            );
                        }
                        delete [] args;             /* exit(EXIT_FAILURE);  */
                    }
                }
                else
                {
                    util::status_message
                    (
                        "Forked to child, PID", std::to_string(pid)
                    );
                    s_nsmd_child_pid = pid;
                }
            }
        }
        for (;;)
        {
            (void) sleep(1);
            if (ctlr.osc_active())
            {
                util::info_message("Going active");
                break;
            }
            else if (s_die_now)
                exit(EXIT_SUCCESS);
        }
        if (s_do_ping)
        {
            util::status_message("Pinging...");
            if (! ctlr.ping())
                exit(EXIT_FAILURE);
        }
        if (s_action_tag != osc::tag::illegal)
        {
            if (s_action_tag == osc::tag::srvlist)
            {
                std::string sessions = ctlr.get_session_list();
                util::status_message("Available sessions:");
                printf("%s", CSTR(sessions));
            }
            else
            {
                bool ok;
                if (s_is_client_action)
                    ok = ctlr.send_client_message(s_action_tag, s_subject_name);
                else
                    ok = ctlr.send_server_message(s_action_tag, s_subject_name);

                if (ok)
                    util::info_message("Action sent");
                else
                    util::error_message("Action failed to send");
            }
        }

        /*
         * Unlike the GUI, this application does its one thing and then
         * exits, unless --monitor is provided. Should we use
         * endpoint::run() in that case?
         */

        if (s_do_monitor)
        {
            util::status_message("Monitoring. Hit Ctrl-C to quit.");
            for (;;)
            {
                ctlr.osc_wait(1000);       /* wait and check for messages      */
                if (! ctlr.osc_active())
                    break;
            }
        }
        else
        {
            util::info_message("Waiting 1 second");
            ctlr.osc_wait(1000);        /* wait and check for messages      */
        }
        if (s_do_stop)
            ctlr.quit();
    }
    else
    {
        util::error_message("Could not create OSC server");
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

/*
 * nsmctl.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */
