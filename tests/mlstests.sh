#!/bin/bash
#
#******************************************************************************
# mlstests.sh
#------------------------------------------------------------------------------
##
# \file           mlstests.sh
# \library        Any project
# \author         Chris Ahlstrom
# \date           2025-03-26
# \update         2025-03-26
# \version        $Revision$
# \license        $XPC_SUITE_GPL_LICENSE$
#
#     The above is modified by the following to remove even the mild GPL
#     restrictions:
#
#     Use this script in any manner whatsoever.  You don't even need to give
#     me any credit.  However, keep in mind the value of the GPL in keeping
#     software and its descendant modifications available to the community
#     for all time.
#
#     This script runs some tests of nsmctl. More description to come.
#
#     At the moment it starts only nsmd so that we can debug.
#
#------------------------------------------------------------------------------


if [ "$1" == "--help" ] || [ "$1" == "help" ] ; then

   cat << E_O_F
Usage v. 2025-03-26

   mlstests.sh [ options ] run some apps and tests.

Options:

   --list      List the drivers and the ALSA/JACK parameters. [Default]

E_O_F

exit 0

else

   jackctl --start --a2j
   nsmd --load-session 2025-01-26

fi

#******************************************************************************
# mlstests.sh
#------------------------------------------------------------------------------
# vim: ts=3 sw=3 et ft=sh
#------------------------------------------------------------------------------
