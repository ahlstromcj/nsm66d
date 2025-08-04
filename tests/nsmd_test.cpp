/*
 *  This file is part of nsm66.
 *
 *  nsm66 is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation; either version 2 of the License, or (at your option) any later
 *  version.
 *
 *  nsm66 is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with nsm66; if not, write to the Free Software Foundation, Inc., 59 Temple
 *  Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * \file          nsmd_test.cpp
 *
 *      A test-file for the nsmd66 application.  TODO.
 *
 * \library       nsm66
 * \author        Chris Ahlstrom
 * \date          2025-01-29
 * \updates       2025-03-20
 * \license       See above.
 *
 */

#include <string>                       /* std::string                      */
#include <cstdlib>                      /* EXIT_SUCCESS, EXIT_FAILURE       */
#include <iostream>                     /* std::cout                        */

#include "cpp_types.hpp"                /* lib66::tokenization              */
#include "cfg/appinfo.hpp"              /* cfg::appinfo                     */
#include "cli/parser.hpp"               /* cli::parser, etc.                */

/**
 *  A more extensive list of options is tested in the ini_test program.
 */


/*
 * Explanation text.
 */

static const std::string s_help_intro
{
    "This test program (WHICH IS NOT READY) illustrates/tests \n"
    "the nsm66d application.  The options available are as follows:\n\n"
    "    none\n"
};

/*
 * Local options. Specified here for use with --help.
 */

cfg::options::container s_test_options
{
    /*
     *  Name
     *      Code,  Kind, Enabled,
     *      Default, Value, FromCli, Dirty,
     *      Description, Built-in
     */
    {
        {
            "bogus",                    /* 'b' or cfg::options::code_null   */
            {
                'b', cfg::options::kind::boolean, cfg::options::enabled,
                "false", "", false, false,
                "If specified, the test of bogus is run by itself.",
                false
            }
        },
    }
};

const std::string s_description
{
    "This test is not yet written.\n"
};

/*
 * main() routine
 */

int
main (int argc, char * argv [])
{
    int rcode = EXIT_FAILURE;
    cfg::set_client_name("nsmd");                   /* for error_message()  */
    cli::parser clip(s_test_options, "", "");
    bool success = clip.parse(argc, argv);
    if (success)
    {
        rcode = EXIT_SUCCESS;

        /*
         *  The application can substitute its own code for the common
         *  options, which are always present.
         */

        if (clip.help_request())
        {
            std::cout << s_help_intro << clip.help_text();
        }
        if (clip.show_information_only())           /* clip.help_request()  */
        {
            if (clip.description_request())
            {
                std::cout << s_description;
            }
        }
        if (clip.version_request())
        {
            // std::cout << nsm66d_version() << std::endl;
        }
        if (clip.verbose_request())
        {
            // nothing yet
        }
        if (clip.inspect_request())
        {
            util::error_message("--investigate unsupported in this program");
            success = false;
        }
        if (clip.investigate_request())
        {
            util::error_message("--investigate unsupported in this program");
            success = false;
        }
    }
    else
        std::cerr << "Command-line parsing failed" << std::endl;

    if (success)
        std::cout << "nsmd_test C++ test succeeded" << std::endl;
    else
        std::cerr << "nsmd_test C++ test failed" << std::endl;

    return rcode;
}

/*
 * nsmd_test.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

