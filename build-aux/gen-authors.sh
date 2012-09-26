#! /bin/sh


echo '# Copyright (c) 2007-2012 Nicta, OML Contributors <oml-user@lists.nicta.com.au>'
echo '#'
echo '# Licensing information can be found in file COPYING.'
echo ''

(
  # Pre-git authors
  echo -e 'Manpreet Singh\nIvan Seskar\nPandurang Kama' # Some authors of "ORBIT Measurements Framework and Library (OML): Motivations, Design, Implementation, and Features" (others in Git log)
  git log | sed -n -e 's/^Author: *//p'
) \
  | sed -e 's/ *<.*>.*//' \
  | sort \
  | uniq
