/*
 *  Copyright (C) 2008-2020 Jonathan Moore Liles (as "Non-Session-Manager")
 *  Copyright (C) 2020- Nils Hilbricht
 *
 *  This file is derived from New-Session-Manager's jackpatch.c
 *
 *  New-Session-Manager is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  New-Session-Manager is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/**
 * \file          jackpatch66.cpp
 *
 *    This module refactors the endpoing class to replace C code with
 *    C++ code.
 *
 * \library       jackpatch66 application
 * \author        Chris Ahlstrom and other authors; see documentation
 * \date          2025-02-14
 * \updates       2025-04-20
 * \version       $Revision$
 * \license       GNU GPL v2 or above
 *
 *     This program is just like ASSPatch, except that it works with Jack
 *     ports (audio and MIDI).
 *
 *  How this application works:
 *
 *      -   Get the command-line options.
 *      -   Create the JACK client and JACK ringbuffer.
 *      -   more...
 */

#include <algorithm>                    /* std::sort()                      */
#include <cerrno>                       /* #include <errno.h>               */
#include <csignal>                      /* std::signal() and <signal.h>     */
#include <cstring>                      /* std::strerror()                  */
#include <cstdlib>                      /* std::getenv(), std::rand()       */
#include <getopt.h>

#include "jackpatch66.hpp"              /* application data types           */
#include "cfg/appinfo.hpp"              /* cfg66: cfg::set_client_name()    */
#include "nsm/helpers.hpp"              /* nsm66: nsm::extract_xxx() funcs  */
#include "osc/lowrapper.hpp"            /* nsm66: LO_TT_IMMEDIATE_2         */
#include "osc/messages.hpp"             /* nsm66: osc::tag enumeration      */
#include "util/msgfunctions.hpp"        /* cfg66: util::info_printf()       */
#include "util/filefunctions.hpp"       /* cfg66: util::file_write_lines()  */
#include "util/strfunctions.hpp"        /* cfg66: util::string_asprintf()   */

#define USE_SAMPLE_CODE                 // EXPERIMENTAL

#define JACKPATCH66_JACK_SUPPORT
#define JACKPATCH66_LIBLO_SUPPORT

#define JACKPATCH66_APP_TITLE "jackpatch66"
#define JACKPATCH66_CLIENT_NAME "jp66"

#if ! defined JACKPATCH66_VERSION
#define JACKPATCH66_VERSION "1.0.0"
#endif

#if defined JACKPATCH66_JACK_SUPPORT
#include <jack/jack.h>
#else
#error Support for JACK required for this class, install jack2
#endif

#if defined JACKPATCH66_LIBLO_SUPPORT
#include <lo/lo.h>                      /* library for the OSC protocol     */
#else
#error Support for liblo required for this class, install liblo-dev
#endif

#include <jack/ringbuffer.h>

#define JACK_RINGBUFFER_SIZE    (1024 * 8)
#define OSC_NAME(name)          osc_ ## name

static lo_server g_lo_server;
static lo_address g_nsm_lo_address;
static std::string g_project_file;
static int g_jack_portname_sz = 0;  // defined after jack client activated
static bool g_nsm_is_active = false;
static bool g_die_now = false;
static patch_list g_patch_list;
static port_list g_known_ports;

/*
 * Pre-declarations of some functions defined in this module.
 */

jack_client_t * jackpatch_client ();
jack_ringbuffer_t * jackpatch_ringbuffer ();
void inactivate_patch (const std::string & portname);

/*
 *  Helper functions.
 */

static std::string
make_client_port_name (const client_port & cp)
{
    return util::string_asprintf("%s:%s", V(cp.client), V(cp.port));
}

static void
preserving_msg (const std::string & dir, const std::string & clientport)
{
    util::info_printf
    (
        "We remember %s %s, but it does not exist anymore. "
        "Making sure it will not be forgotten.",
        V(dir), V(clientport)
    );
}

/**
 *  Pretty-print patch relationship of the patch-record. The mode
 *  is the return code fropm jack_connect() converted to bool.
 *  If true, an error occurred.
 */

void
print_patch (const patch_record & pr, bool iserror)
{
    if (util::investigate() || iserror)
    {
        std::string fmt = iserror ?
            "? From '%s:%s' to\n         '%s:%s' ?" :
            "From '%s:%s' to\n         '%s:%s'" ;

        util::info_printf
        (
            fmt,
            V(pr.pr_src.client), V(pr.pr_src.port),
            V(pr.pr_dst.client), V(pr.pr_dst.port)
        );
    }
}

void
enqueue (patch_record & p)
{
    g_patch_list.push_back(p);
}

/*
 *  Don't need a separate function. Make it similar to enqueue().
 *
 *      enqueue_port(&g_known_ports, port);
 */

void
enqueue_known_port (const std::string & portname)
{
    port_record p { portname };
    g_known_ports.push_back(p);
}

/**
 *  Find a jack port in our own data structure and not in the jack graph.
 */

std::string
find_known_port (const std::string & portname)
{
    std::string result;
    for (const auto & pr : g_known_ports)
    {
        if (pr.port == portname)
        {
            result = pr.port;
            break;
        }
    }
    return result;
}

/*
 *  Remove it from the list of known ports. This is done by getting
 *  the port_record's "next" pointer, assigning it, then deleting
 *  the port record.
 *
 *  Then mark all patches including this port as inactive.
 */

void
remove_known_port (const std::string & portname)
{
    port_list & known = g_known_ports;
    for (port_list::iterator i = known.begin(); i != known.end(); ++i)
    {
        if (i->port == portname)
        {
            known.erase(i);
            break;
        }
    }
    inactivate_patch(portname);
}

/**
 * Convert a symbolic string of a JACK connection into an actual data struct
 * patch_record.
 *
 *  The name of the patch file is like this: "JACKPatch.nLWNW.jackpatch".
 *
 *  Examples of patches in this file. All items are one-liners, even if long:
 *
 *      PulseAudio JACK Sink:front-left |> system:playback_1
 *
 *      a2j:Launchpad Mini (capture): Launchpad Mini MIDI 1 |>
 *              seq66.nPSLM:a2j:Launchpad Mini (capture): Launchpad Mini MIDI 1
 *
 *      seq66.nPSLM:a2j:Launchpad Mini (playback): Launchpad Mini MIDI 1 |>
 *              a2j:Launchpad Mini (playback): Launchpad Mini MIDI 1
 *
 *      seq66.nPSLM:fluidsynth-midi:midi_00 |> fluidsynth-midi:midi_00
 *
 *      " %m[^:]:%m[^|] |%1[<>|] %m[^:]:%m[^\n]",
 *       ^ ^ ^  ^   ^   ^ ^ ^^    ^
 *       | | |  |   |   | | dir   |
 *       | | |  |   |   | |        -----> The rest are simple.
 *       | | |  |   |   |  ----> Get only one of the <, >, or | characters.
 *       | | |  |   |    ------> Absorb the pipe
 *       | | |  |    ----------> Fetch all characters up to the pipe
 *       | | |   --------------> Absorb the colon
 *       | |  -----------------> See "[^:]" notes below
 *       |  -------------------> A character pointer allocated by sscanf()
 *        ---------------------> Throw-away white space
 *
 *  "[^:]"
 *
 *      This token suppresses white-space scanning. The characters in the
 *      square brackets are the characters to be accepted as data; but here,
 *      the "^:" means "accept all characters that are not a colon".
 *
 *  Issue:
 *
 *      One issue is that, with a2jmidid running (on older JACK setups),
 *      the client-name itself will have a colon, screwing up the parsing
 *      above.
 *
 *  At some point we would like to use something a bit easier to read, such
 *  as the INI or JSON formats. XML, as used in ajsnapshot, is overkill.
 *
 *  The 5 elements processed are the source client and port, the destination
 *  client and port, and the directional indicator ("<", ">", and "|>".
 *
 * \return
 *      Returns 0 if the 5 elements were not found. Returns -1 if an EOF was
 *      encountered.
 *
 * Warning:
 *
 *      ISO C++11 doesn't support the 'm' scanf() flag. Therefore we replace it
 *      with a new process_patch(), which calls nsm::process_patch().
 */

#if defined USE_PROCESS_PATCH_SSCANF

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"

bool
process_patch_sscanf (const std::string & patch)
{
    /*
     * Pointers filled in by %m by sscanf().
     */

    char * leftc;               /* left capture client  */
    char * leftp;               /* left playback port   */
    char * rightc;              /* right capture client */
    char * rightp;              /* right playback port  */
    char dir[4];                /* < or > direction     */
    int count = sscanf
    (
        CSTR(patch), " %m[^:]:%m[^|] |%1[<>|] %m[^:]:%m[^\n]",
        &leftc, &leftp, dir, &rightc, &rightp
    );
    if (count == EOF || count != 5)
        return false;

    /*
     * Trim space
     */

    for (int j = strlen(leftp) - 1; j > 0; --j)
    {
        if (leftp[j] == ' ' || leftp[j] == '\t')
            leftp[j] = 0;
        else
            break;
    }
    dir[2] = 0;

    patch_record pr;
    switch (dir[0])
    {
        case '<':                   /* this character is not used, afaict   */

            pr.pr_src.client = rightc;
            pr.pr_src.port   = rightp;
            pr.pr_dst.client = leftc;
            pr.pr_dst.port   = leftp;
            enqueue(pr);
            break;

        case '>':

            pr.pr_src.client = leftc;
            pr.pr_src.port   = leftp;
            pr.pr_dst.client = rightc;
            pr.pr_dst.port   = rightp;
            enqueue(pr);
            break;

        case '|':

            {
                pr.pr_src.client = rightc;
                pr.pr_src.port   = rightp;
                pr.pr_dst.client = leftc;
                pr.pr_dst.port   = leftp;
                enqueue(pr);

                patch_record pr2;
                pr2.pr_src.client = leftc;
                pr2.pr_src.port   = leftp;
                pr2.pr_dst.client = rightc;
                pr2.pr_dst.port   = rightp;
                enqueue(pr2);
            }
            break;

        default:

            return false;
    }
    pr.pr_active = false;
    print_patch(pr, false);         /* no error detectable currently */
    return true;
}

#pragma GCC diagnostic pop

#endif  // defined USE_PROCESS_PATCH_SSCANF

bool
process_patch (const std::string & patch)
{
    std::string leftc;               /* left capture client  */
    std::string leftp;               /* left playback port   */
    std::string rightc;              /* right capture client */
    std::string rightp;              /* right playback port  */
    nsm::patch_direction dir = nsm::process_patch
    (
        patch, leftc, leftp, rightc, rightp
    );
    if (dir == nsm::patch_direction::error)
    {
        util::error_message("Failed to parse", patch);
        return false;
    }

    /*
     * Trim space? Why? If needed, do it in nsm::process_patch().
     */

#if 0
    for (int j = strlen(leftp) - 1; j > 0; --j)
    {
        if (leftp[j] == ' ' || leftp[j] == '\t')
            leftp[j] = 0;
        else
            break;
    }
    dir[2] = 0;
#endif

    patch_record pr;
    switch (dir)
    {
        case nsm::patch_direction::left:    /* '<' char not used, AFAICT    */

            pr.pr_src.client = rightc;
            pr.pr_src.port   = rightp;
            pr.pr_dst.client = leftc;
            pr.pr_dst.port   = leftp;
            enqueue(pr);
            break;

        case nsm::patch_direction::right:   /* '>' character                */

            pr.pr_src.client = leftc;
            pr.pr_src.port   = leftp;
            pr.pr_dst.client = rightc;
            pr.pr_dst.port   = rightp;
            enqueue(pr);
            break;

        case nsm::patch_direction::duplex:  /* '|' char not used, AFAICT    */

            {
                pr.pr_src.client = rightc;
                pr.pr_src.port   = rightp;
                pr.pr_dst.client = leftc;
                pr.pr_dst.port   = leftp;
                enqueue(pr);

                patch_record pr2;
                pr2.pr_src.client = leftc;
                pr2.pr_src.port   = leftp;
                pr2.pr_dst.client = rightc;
                pr2.pr_dst.port   = rightp;
                enqueue(pr2);
            }
            break;

        default:

            util::error_message("Bad patch");
            return false;
    }
    pr.pr_active = false;
    print_patch(pr, false);         /* no error detectable currently */
    return true;
}

/**
 *  Deletes all patch records in the patch-list.
 */

void
clear_all_patches ()
{
    g_patch_list.clear();
}

/**
 *  Crudely parse configuration file named by /file/ using fscanf().
 *  The process is:
 *
 *      1. Open the file.
 *      2. Clear all existing patches.
 *      3. Read a line c
 *      kharacter-by-character.
 *      4. Process the line, which contains a connection.
 *      5. Close the file.
 *
 *  We now use lib66/cfg66 library items and functions. More work
 *  behind the scenes, but easier on the caller.
 */

bool
read_config (const std::string & file)
{
    util::status_message("Reading connections", file);
    lib66::tokenization lines;
    bool result = util::file_read_lines(file, lines);
    if (result)
    {
        int count = 0;
        clear_all_patches();
        for (const auto & buf : lines)
        {
            ++count;
            result = process_patch(buf);        /* calls sscanf()           */
            if (! result)
                util::warn_printf("Bad line %i", count);
        }
    }
    return result;
}

/**
 *  A connection attempt will only be made when a JACK port registers itself
 *  and we receive the JACK callback, and once on startup.  There is no
 *  periodic check if a previously saved connection is still alive. This
 *  is by design.
 *
 *  Returns 0 if connection failed, true if succeeded.
 *  Already connected is not considered a failure.
 *
 * Notes:
 *
 *      1.  These should really be g_jack_portname_sz, but in the real world
 *          not every system and compiler does C99.
 *
 *      2.  Since we only connect KNOWN TO US ports a connection will not be
 *          made on startup / file load, even if both jack ports are actually
 *          present in JACK. This is because we have not parsed both ports
 *          yet. The true connection attempt will be made only when the
 *          second port of the pair was parsed.
 *
 *          The log message below is misleading for users, because nothing is
 *          wrong, and should only be used during development.
 *
 *          We just skip the first attempt, eventhough JACK will not complain
 *          and do nothing wrong.
 *
 *          That also means that we do not detect actually missing ports.
 */

void
connect_path (patch_record & pr)
{
    if (pr.pr_active)
    {
        /*
         * The patch is already active, don't bother JACK with it...
         */

        return;
    }
    else
    {
        std::string srcport{make_client_port_name(pr.pr_src)};
        std::string dstport{make_client_port_name(pr.pr_dst)};
        bool srcmatch = ! find_known_port(srcport).empty();
        bool dstmatch = ! find_known_port(dstport).empty();
        if (! srcmatch || ! dstmatch)
            return;                         /* Note 2.      */

        int rc = ::jack_connect
        (
            jackpatch_client(), CSTR(srcport), CSTR(dstport)
        );
        print_patch(pr, rc != 0);
        if (rc == 0 || rc == EEXIST)
        {
            pr.pr_active = true;
            return;
        }
        else
        {
            pr.pr_active = false;
            util::error_printf("JACK connect error %i", rc);
            return;
        }
    }
}

void
do_for_matching_patches
(
    const std::string & fullportname,
    patchfunc func                      // void (* func)(patch_record &)
)
{
#if defined USE_PROCESS_PATCH_SSCANF
    char client[512];                           /* Linux JACK limit is 64   */
    char port[512];                             /* Linux JACK limit is 256  */
    sscanf(CSTR(fullportname), "%[^:]:%[^\n]", client, port);
#endif

    std::string client;
    std::string port;
    bool ok = nsm::extract_client_port(fullportname, client, port);
    if (ok)
    {
        for (auto & pr : g_patch_list)
        {
            bool srcmatch =
                client == pr.pr_src.client && port == pr.pr_src.port;

            bool dstmatch
                = client == pr.pr_dst.client && port == pr.pr_dst.port;

            if (srcmatch || dstmatch)
                func(pr);
        }
    }
}

/**
 *  Callback functions for use by do_for_matching_patches().
 */

void
inactivate_path (patch_record & pr)
{
    pr.pr_active = false;
}

void
inactivate_patch (const std::string & portname)
{
    do_for_matching_patches(portname, inactivate_path);
}

void
activate_patch (const std::string & portname)
{
    do_for_matching_patches(portname, connect_path);
}

/**
 *  Called for every new port, which includes restored-from-file ports on
 *  startup.  It will try to activate a restored connection for every single
 *  port, thus doing an attempt twice: Once for the source-port and then for
 *  the destination-port.
 */

void
handle_new_port (const std::string & portname)
{
    enqueue_known_port(portname);
    activate_patch(portname);                       /* this is a new port   */
    util::info_message("New endpoint registered", portname);
}

void
register_prexisting_ports ()
{
    const char ** ports = ::jack_get_ports(jackpatch_client(), NULL, NULL, 0);
    if (not_nullptr(ports))
    {
        const char ** p;
        for (p = ports; not_nullptr(*p); ++p)
        {
            std::string portname {*p};
            handle_new_port(portname);
        }
        ::jack_free(ports);
    }
}

/**
 *  Save all current connections to a file.
 *
 *  Strategy:
 *
 *      -   If there are no JACK ports at all don't do anything.
 *      -   Otherwise: Remember all currently known connections where
 *          one, or both, ports are missing from the JACK graph.
 *          We consider these temporarily gone by accident.
 *      -   Clear the current save file.
 *      -   For each currently existing JACK output port, save all of
 *          it's connections.
 *      -   Save all these port-pairs in an empty file. Ports without
 *          connections are not saved.
 *
 *  Notes:
 *
 *      -   Temporary table.
 *          Prepare a temporary table where all connection strings are held
 *          until the file is written at the bottom of this function.
 *          We first add all connections that are temporarily out of order
 *          (see below) and then all currently existing connections.
 *          With C++, we can avoid malloc() and much more by using
 *          lib66::tokenization(), a sortable vector of strings.
 *      -   JACK graph.
 *          Before we forget the current state find all connections that we
 *          have in memory but where one or both ports are currently missing
 *          in the JACK graph.  We don't want to lose connections that are
 *          just temporarily not present.
 *      -   Patch description.
 *          A patch is one connection between a source and a destination.
 *          If an actual jack port source is connected to more than one
 *          destinations it will appear as it's own "patch" in this list.
 *          We only need to consider 1:1 point connections in this loop.
 *      -   Remember missing ports, part 1.
 *          The port does not exist anymore. We need to remember it!
 *          It doesn't matter if the destination port still exists,
 *          The file-writing below will only consider ports that are
 *          currently present and connected.
 *      -   Check connection.
 *          The source port does still exist, but is it's connection
 *          still alive? Do not use jack_port_get_all_connections();
 *          we want to know if a specific destination is still there.
 *          The client is our own jackpatch-jack-client.
 *      -   Magic string.
 *          prepare the magic string that is the step before creating
 *          a struct from with process_patch. port is source client:port
 *          and connection is the destination one.
 *
 */

bool
snapshot (const std::string & file)
{
    const char ** jports = ::jack_get_ports
    (
        jackpatch_client(), NULL, NULL, JackPortIsOutput
    );
    if (is_nullptr(jports))
    {
        util::warn_message("Could not get JACK ports");
        return false;
    }

    /*
     * Temporary table. See function banner.
     */

    lib66::tokenization table;

    /*
     * JACK graph. See function banner.
     */

    for (auto & pr : g_patch_list)
    {
        bool remember_this_connection = false;

        /*
         * Patch description. See function banner. Traverse the list.
         */

        std::string src_client_port { make_client_port_name(pr.pr_src) };
        std::string dst_client_port { make_client_port_name(pr.pr_dst) };
        jack_port_t * jp_t_src = ::jack_port_by_name
        (
            jackpatch_client(), CSTR(src_client_port)
        );
        if (! jp_t_src)
        {
            /*
             * Remember missing ports. See function banner.
             */

            preserving_msg("source", src_client_port);
            remember_this_connection = true;
        }
        else
        {
            /*
             * Check connection. See function banner.
             */

            jack_port_t * jp_t_dst = ::jack_port_by_name
            (
                jackpatch_client(), V(dst_client_port)
            );
            if (! jp_t_dst)
            {
                /*
                 * Remember missing ports. See function banner.
                 */

                preserving_msg("destination", dst_client_port);
                remember_this_connection = true;
            }
        }
        if (remember_this_connection)
        {
            /*
             * Why 40?
             *
             * "%-40s |> %s\n", V(src_client_port), V(dst_client_port)
             */

            std::string fmt = util::investigate() ?
                "'%s' |> '%s'\n" : "%s |> %s\n" ;

            std::string s = util::string_asprintf
            (
                fmt, V(src_client_port), V(dst_client_port)
            );
            table.push_back(s);

            /*
             * process_patch(s): infinite loop! Still need to keep these
             * patch_records! See below.
             */

            util::info_printf
            (
                "Remember ++ %s |> %s", V(src_client_port), V(dst_client_port)
            );
        }
    }
    clear_all_patches();                        /* tabula rasa              */

    /*
     * We just removed the patch_records we wanted to remember. The size
     * of the table vector holds the number of remembered records, if
     * any.
     */

    for (const auto & r : table)
    {
        bool ok = process_patch(r);
        if (! ok)
        {
            util::warn_message("Could not process", r);
            break;
        }
    }

    const char ** jport;
    for (jport = jports; not_nullptr(*jport); ++jport)  /* jack_get_ports() */
    {
        /*
         * *jport is a full client:port JACK name, not just the port.
         */

        jack_port_t * jp = ::jack_port_by_name(jackpatch_client(), *jport);
        if (not_nullptr(jp))
        {
            if (util::investigate())
                util::info_message("JACK port found", *jport);
        }
        else
        {
            util::warn_message("JACK port not found", *jport);
            continue;
        }

        const char ** connections = ::jack_port_get_all_connections
        (
            jackpatch_client(), jp
        );
        if (is_nullptr(connections))
        {
            util::warn_message("No connections for port", *jport);
            continue;
        }

        const char ** c;
        for (c = connections; not_nullptr(*c); ++c)
        {
            /*
             * Prepare the magic string that is the step before creating
             * a struct with process_patch(). jport is source client:port
             * and c (connection) is the destination one.
             *
             * Why 40?
             *
             * "%-40s |> %s\n", V(src_client_port), V(dst_client_port)
             */

            std::string fmt = util::investigate() ?
                "'%s' |> '%s'\n" : "%s |> %s\n" ;

            std::string s = util::string_asprintf
            (
                fmt, *jport, *c
            );
            table.push_back(s);
            if (util::investigate())
                util::info_message("Patch", s);

            bool ok = process_patch(s);
            if (! ok)
            {
                util::warn_message("Could not process", s);
                break;
            }

            /*
             * Verbose output that an individual connection was saved.
             * util::info_printf(" ++ %s |> %s\n", *jport, *c);
             */
        }
        free(connections);
    }
    free(jports);

    /*
     * Sort and write to file
     */

    std::sort(table.begin(), table.end());
    bool success = util::file_write_lines(file, table);
    return success;
}

void
signal_handler (int x)
{
    util::status_printf("Handle signal %d\n", x);
    g_die_now = true;
}

void
die ()
{
    if (not_nullptr(jackpatch_client()))
    {
        jack_deactivate(jackpatch_client());
        util::status_message("Closing jack client");
        jack_client_close(jackpatch_client());
    }
    exit(0);
}

/**
 *  Handle signals. Not used: SIGQUIT; SIGSEGV; and SIGPIPE.
 */

void
set_traps ()
{
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

/*
 * OSC HANDLERS
 */

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
    snapshot(g_project_file);
    lo_send_from
    (
        g_nsm_lo_address, g_lo_server, LO_TT_IMMEDIATE_2,
        "/reply", "ss", path, "OK"
    );
    return osc::osc_msg_handled();
}

/*
 * g_jack_portname_sz is a global which is client+port+1 = 64 + 256 + 1
 * = 321 on Linux. Use 512 to be (ha ha) safe.
 */

void
maybe_activate_jack_client ()
{
    if (not_nullptr(jackpatch_client()))           //  if (! g_client_active)
    {
        ::jack_activate(jackpatch_client());       // no test of success?
        g_jack_portname_sz = ::jack_port_name_size();

    }
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
    if (argc >= 1)
    {
        std::string newpath { &argv[0]->s };

#if USE_THIS_CODE
        std::string displayname;
        if (argc >= 2)
            displayname = &argv[1]->s;
#endif

        std::string newfilename = util::string_asprintf
        (
            "%s.jackpatch", V(newpath)
        );
        if (util::file_status(newfilename))
        {
            if (read_config(newfilename))
            {
                /*
                 * wipe_ports();
                 * check_for_new_ports();
                 */

                maybe_activate_jack_client();
                register_prexisting_ports();
            }
            else
            {
                lo_send_from
                (
                    g_nsm_lo_address, g_lo_server, LO_TT_IMMEDIATE_2,
                    "/error", "sis", path, -1, "Could not open file"
                );
                return osc::osc_msg_handled();
            }
        }
        else
        {
            maybe_activate_jack_client();
            clear_all_patches();
        }
        g_project_file = newfilename;
        lo_send_from
        (
            g_nsm_lo_address, g_lo_server, LO_TT_IMMEDIATE_2,
            "/reply", "ss", path, "OK"
        );
    }
    return osc::osc_msg_handled();
}

#if 0

void
announce
(
    const std::string nsm_url,
    const std::string client_name,
    const std::string process_name
)
{
    util::info_message("Announcing to NSM");

    lo_address to = lo_address_new_from_url(CSTR(nsm_url));
    int pid = int(getpid());
    lo_send_from
    (
        to, g_lo_server, LO_TT_IMMEDIATE_2,
        "/nsm/server/announce", "sssiii",       /* osc::tag::srvannounce    */
        CSTR(client_name),
        ":switch:",
        CSTR(process_name),
        NSM_API_VERSION_MAJOR,                      /* 0 api_major_version  */
        NSM_API_VERSION_MINOR,                      /* 0 api_minor_version  */
        pid
    );
    lo_address_free(to);
}

#endif

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
            g_lo_server, CSTR(msg), OPTR(pattern), f, NULL // CSTR(params)
        );
        if (! params.empty())
            util::info_message("Method parameters", params);
    }
}

void
add_methods ()
{
    add_method(osc::tag::clisave, OSC_NAME( save ), "");
    add_method(osc::tag::cliopen, OSC_NAME( open ), "");
    add_method(osc::tag::error, OSC_NAME( announce_error ), "");
    add_method(osc::tag::replyex, OSC_NAME( announce_reply ), "");
}

void
init_osc (const std::string & oscport = "")
{
    char * p = oscport.empty() ? NULL : STR(oscport) ;
    g_lo_server = lo_server_new(p, NULL);

    char * url = lo_server_get_url(g_lo_server);
    if (not_nullptr(url))
    {
        util::info_message("OSC", std::string(url));
        free(url);
    }
    add_methods();
#if defined USE_OLD_CODE
    lo_server_add_method
    (
        g_lo_server, "/nsm/client/save", "", osc_save, NULL
    );
    lo_server_add_method
    (
        g_lo_server, "/nsm/client/open", "sss", osc_open, NULL
    );
    lo_server_add_method
    (
        g_lo_server, "/error", "sis", osc_announce_error, NULL
    );
    lo_server_add_method
    (
        g_lo_server, "/reply", "ssss", osc_announce_reply, NULL
    );
#endif
}

/**
 *  Reads the JACK ring--buffer into a new port_notification_record.
 *  The caller must check the pointer, then delete it after processing
 *  it.
 */

port_notification_record *
dequeue_new_port ()
{
    int sz = 0;
    int peeksz = ::jack_ringbuffer_peek
    (
        jackpatch_ringbuffer(), (char *) &sz, sizeof(int)
    );
    if (peeksz == sizeof(int))
    {
        size_t space = ::jack_ringbuffer_read_space(jackpatch_ringbuffer());
        if (space >= size_t(sz))
        {
            port_notification_record * pr =
                new (std::nothrow) port_notification_record;

            ::jack_ringbuffer_read(jackpatch_ringbuffer(), (char *) pr, sz);
            return pr;
        }
    }
    return nullptr;
}

void
check_for_new_ports ()
{
    port_notification_record * p = nullptr;
    while (not_nullptr(p = dequeue_new_port()))
    {
        if (p->pnr_registered)
            handle_new_port(std::string(p->pnr_port));
        else
            remove_known_port(std::string(p->pnr_port));

        delete p;
    }
}

void
port_registration_callback (jack_port_id_t id, int reg, void * /*arg*/)
{
    jack_port_t * p = ::jack_port_by_id(jackpatch_client(), id);
    const char * jport = ::jack_port_name(p);
    size_t sz = std::strlen(jport) + sizeof(port_notification_record) + 1;
    port_notification_record * pr =
        new (std::nothrow) port_notification_record;

   /*
    * This needs some INVESTIGATION
    */

    if (not_nullptr(pr))
    {
        pr->pnr_length = sz;
        pr->pnr_registered = reg;
        (void) std::snprintf                        /* pr->pnr_port = jport */
        (
            pr->pnr_port, sizeof pr->pnr_port, "%s", jport
        );

        size_t jsize = ::jack_ringbuffer_write
        (
            jackpatch_ringbuffer(), (const char *) pr, sz
        );
        if (sz != jsize)
            util::error_message("JACK port notification buffer overrun");

        // enqueue_new_port(jport, reg);
    }
}

/*
 * Sets up JACK. Called again and again to get the JACK client pointer.
 */

jack_client_t *
jackpatch_client ()
{
    static bool s_uninitialized = true;
    static jack_client_t * s_jack_client = nullptr;
    if (s_uninitialized)
    {
        jack_status_t jstatus;
        s_jack_client = ::jack_client_open
        (
            JACKPATCH66_APP_TITLE, JackNullOption, &jstatus
        );
        ::jack_set_port_registration_callback
        (
            s_jack_client, port_registration_callback, NULL     /* no arg   */
        );
        s_uninitialized = false;
        if (not_nullptr(s_jack_client))
            util::info_message("JACK client created");
        else
            util::error_message("JACK client could not open");
    }
    return s_jack_client;
}

/*
 *  Sets up the JACK ringbuffer. Called again and again to get the JACK
 *  ringbuffer pointer.
 *
 *  Do we want to use our own ring-buffer? Nah.
 */

jack_ringbuffer_t *
jackpatch_ringbuffer ()
{
    static bool s_uninitialized = true;
    static jack_ringbuffer_t * s_jack_ringbuffer = nullptr;
    if (s_uninitialized)
    {
        s_jack_ringbuffer = ::jack_ringbuffer_create(JACK_RINGBUFFER_SIZE);
        s_uninitialized = false;
        if (not_nullptr(s_jack_ringbuffer))
            util::info_message("JACK ringbuffer created");
        else
            util::error_message("JACK ringbuffer not created");
    }
    return s_jack_ringbuffer;
}

/*
 * Print usage message according to POSIX.1-2017
 */

void help ()
{
    static std::string usage
    {

"jackpatch66\n"
"\n"
"Remember and restore the JACK Audio Connection Kit graph.\n"
"\n"
"It is intended as module for the 'New Session Manager' and communicates\n"
"over OSC in an NSM-Session.\n"
"\n"
"It has limited standalone functionality for testing and debugging, such as:\n"
"\n"
"   TO DO\n"
"\n"
"Usage:\n"
"\n"
"   jackpatch               Run as an NSM client.\n"
"   jackpatch file          Restore a saved snapshot and monitor it.\n"
"   jackpatch options       See options below.\n"
"\n"
"Options:\n"
"\n"
"   --help          Show this screen and exit\n"
"   --debug         Don't try to connect to NSM, and show verbose status.\n"
"   --verbose       Show informational message.\n"
"   --version       Show version and exit.\n"
"   --save file     Save current connection snapshot to file, then exit.\n"

    };
    puts(CSTR(usage));
}

/*
 *  Main routine
 */

int
main (int argc, char * argv [])
{
    bool no_debug = true;
    util::set_verbose(false);
    util::set_investigate(false);
    cfg::set_client_name(JACKPATCH66_CLIENT_NAME);  /* display in messages  */

    static struct option long_options []
    {
        { "help",       no_argument, 0, 'h' },
        { "debug",      no_argument, 0, 'd' },
        { "save",       no_argument, 0, 's' },
        { "verbose",    no_argument, 0, 'V' },
        { "version",    no_argument, 0, 'v' },
        { 0, 0, 0, 0 }
    };
    int opt_offset = 0;                             /* --debug --save file  */
    int option_index = 0;
    int c = 0;
    while
    (
        (
            c = getopt_long_only(argc, argv, "", long_options, &option_index)
        ) != (-1)
    )
    {
        switch (c)
        {
        case 'h':

            help();
            exit(0);
            break;

        case 'd':

            no_debug = false;
            util::set_investigate(true);
            util::set_verbose(true);
            ++opt_offset;
            break;

        case 's':                               /* save is handled below    */

            break;

        case 'V':

            util::set_verbose(true);
            ++opt_offset;
            break;

        case 'v':

            printf("%s\n", JACKPATCH66_VERSION);
            exit(0);
            break;
        }
    }

    jack_client_t * jc = jackpatch_client();    /* set up, return pointer   */
    if (is_nullptr(jc))
        exit(EXIT_FAILURE);

    jack_ringbuffer_t * jrb = jackpatch_ringbuffer();
    if (is_nullptr(jrb))
        exit(EXIT_FAILURE);

    set_traps();
    if (argc > 1)
    {
        std::string option { argv[opt_offset + 1] };
        maybe_activate_jack_client();
        if (util::strcompare(option, "--save"))
        {
            if (argc > 2)
            {
                /*
                 * To not discard temporarily missing clients we need to
                 * load the current ones from file first, unless debugging.
                 */

                std::string filename { argv[opt_offset + 2] };
                if (no_debug)                           // no --debug
                {
                    if (read_config(filename))          // --save filename
                        register_prexisting_ports();
                }
                util::status_message
                (
                    "Standalone: Saving current graph to", filename
                );
                snapshot(filename);
                die();
            }
            else
            {
                util::error_message("Option needs a parameter", option);
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            /*
             * Enter standalone commandline mode. This is without NSM.
             */

            if (read_config(argv[opt_offset + 1]))
            {
                maybe_activate_jack_client();
                register_prexisting_ports();
            }
            util::info_message("Monitoring in standalone mode...\n" );
            for (;;)
            {
                usleep(50000);
                if (g_die_now)
                    die();

                check_for_new_ports();
            }
        }
    }
    init_osc();
    if (no_debug)
    {
        std::string nsmurl { util::get_env("NSM_URL") };
        if (nsmurl.empty())
            nsmurl = nsm::lookup_active_nsmd_url();

        if (nsmurl.empty())
        {
            util::error_message("Could not register as NSM client");
            exit(EXIT_FAILURE);
        }
        else
        {
            osc::process_announce
            (
                g_lo_server, ":switch:", nsmurl,
                JACKPATCH66_APP_TITLE, argv[0]
            );
        }
    }
    for (;;)
    {
        lo_server_recv_noblock(g_lo_server, 200);
        if (not_nullptr(jackpatch_client()))
            check_for_new_ports();

        if (g_die_now)
            die();
    }
    return EXIT_SUCCESS;
}

/*
 * jackpatch66.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

