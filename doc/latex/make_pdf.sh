#!/bin/sh
#
#******************************************************************************
# make_pdf.sh (meson)
#------------------------------------------------------------------------------
##
# \file       	make_pdf.sh
# \library    	nsm66d
# \author     	Chris Ahlstrom
# \date       	2025-02-03
# \update     	2025-02-03
# \version    	$Revision$
# \license    	$XPC_SUITE_GPL_LICENSE$
#
#     This file creates the LaTeX documentation in the Meson build
#     directory: build/latex.
#
# openout_any:
#
#     Note that we define the openout_any environment variable so that we can
#     call latex-related programs on files in the build directory, which
#     is outside of the doc/latex tree.
#
#------------------------------------------------------------------------------

BUILDDIR="../../build"
LATEXDIR="$BUILDDIR/latex"
PDFDIR="nsm66d/build/latex"
PDFBASENAME="nsm66d-user-guide"
export openout_any="a"

if test "$1" = "--help" ; then

cat << E_O_F

Usage: ./make_pdf.sh

   Run that command from the nsm66d/doc/latex directory. The warnings are
   found in build/latex-warnings.log.  The PDF is found in
   $LATEXDIR.  The work.sh --clean command removes everything in
   $BUILDDIR.

Version: 2025-02-03

E_O_F

   exit 0

else

   echo "Creating $LATEXDIR..."
   mkdir --verbose --parents $LATEXDIR
   if test $? = 0 ; then
      echo "Running latexmk, output in $LATEXDIR, log in $BUILDDIR..."
      latexmk --aux-directory=$LATEXDIR --output-directory=$LATEXDIR -g --silent \
         --pdf tex/$PDFBASENAME > $BUILDDIR/latex-warnings.log

#     if test $? = 0 ; then
#        echo "Reducing margins"
#        sed -e 's/letterpaper,/letterpaper,margin=2cm,/' \
#           -i $BUILDDIR/latex/refman.tex
#     fi

      if test $? = 0 ; then
         echo "The PDF is in $PDFBASENAME.pdf, copied to the doc directory."
         cp $LATEXDIR/*.pdf ../
      else
         echo "PDF build failed, read $PDFDIR/$PDFBASENAME.log"
      fi
   fi

# Optimize the PDF to cut down on its size.

#  if test -f latex/refman.pdf ; then
#     gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dNOPAUSE -dQUIET -dBATCH \
#-sOutputFile=../reference_manual.pdf latex/refman.pdf
#  else
#     echo "ERROR: PDF not generated!"
#     exit 1
#  fi

fi

#******************************************************************************
# make_pdf.sh
#------------------------------------------------------------------------------
# vim: ts=3 sw=3 et ft=sh
#------------------------------------------------------------------------------
