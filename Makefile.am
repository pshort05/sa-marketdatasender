## Process this file with automake to produce Makefile.in
## Created by Anjuta

SUBDIRS = src 

sa_marketdatasenderdocdir = ${prefix}/doc/sa_marketdatasender
sa_marketdatasenderdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS


EXTRA_DIST = $(sa_marketdatasenderdoc_DATA)


# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(sa_marketdatasenderdocdir)
