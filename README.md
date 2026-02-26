# README for Applications Nsm66d 0.1 2026-02-26

__Nsm66d__ is a reimplemation of the Non/New Session Manager applications
__nsmd__, __jackpatch__, and __nsmproxy__. 

This reimplementation was done for various reasons. First, we experienced
some minor issues using the NSM applications, and wanted to learn how
the code works; the best way is to dig into the code and rework it.
Second, much of the original code is basically C code, where, for example,
all the strings are char pointers; C++ provides strings and contains that
are easier to use, plus we like C++. Lastly, we want to beef up the
debugging and testing.

These applications are a work in progress.

Support sites (still in progress):

    *   https://ahlstromcj.github.io/
    *   https://github.com/ahlstromcj/ahlstromcj.github.io/wiki

# Major Features

The "nsm66" directory holds session-management  code.

    *   nsm66d. A reimplementation of the nsmd application. Same features,
        but easier to read and maintain.
    *   jackpatch66. A reimplementation of jackpatch, with fixes to
        the handling of the ports exposed by a2jmidid.
    *   nsm_proxy66. A reimplementation of nsmproxy.
    *   tests. Currently most of the testing is done in the support
        libraries listed below.

    Note that a work.sh script is provided to simplify or clarify various
    operations such as cleaning, building, making a release, and installing
    or uninstalling the library.

    Also note that the two NSM GUI applications are not reimplemented.
    Not interesting in making NSM GUIs.

##  Application Features

    *   Can be built using GNU C++ or Clang C++.
    *   Basic dependencies: Meson 1.1 and above; C++17 and above.
    *   The build system is Meson, and sample wrap files are provided
        for using Nsm66 as a C++ subproject.
    *   PDF documentation built from LaTeX.

##  Application and Library Code

    *   The code is a most hard-core C++, with advanced language features
        used as much as possible.
    *   C++17 and above is required for some of its features.
    *   The GNU and Clang C++ compilers are supported.
    *   Broken into modules for easier maintenance.
    *   Support provided by the following "66" library projects:
        *   lib66. Provides a small collection of headers and data
            type common to all the "66" project.
        *   cfg66. Provides configuration handling, message output,
            string manipulation and parsing, and a lot more.
        *   nsm66. Provides some "nsm" classes used in Seq66, plus
            holds support classes extracted from the NSM code, and
            some helper functions.
        *   potext. Although not yet in use, it can provide for
            internationalizatio of information messages.

##  Fixes

    *   To do.

##  Documentation

    *   To do.

## To Do

    *   Although the libraries above have been configured to use as
        Meson subprojects, the code gets downloaded from GitHub,
        Meson is looking for the installed version and will not fall back
        to the subproject. The solution is to have the nsm66 wrap
        file refer to an accessible git repository: GitHub, local
        SSH server, or local git file URL.
    *   Beef up testing.

## Recent Changes

    For all changes, see the NEWS file.

    *   Version 0.1.0:
        *   Usage of meson instead of autotools, cmake, or qmake.

// vim: sw=4 ts=4 wm=2 et ft=markdown
