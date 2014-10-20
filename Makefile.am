SUBDIRS = primd scripts etc

rpm: dist
	(cd rpm && make) || exit 1

rpmclean:
	(cd rpm && make clean) || exit 1
