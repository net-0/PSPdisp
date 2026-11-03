# This file uses the WINDRIVER_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it VERSION
#
# What the driver packages of the desktop (Windows) share: the version of their INFs.
# The driver packages are included after this one, so they have WINDRIVER_VERSION.

# The version of the packages, which Windows compares to pick the newest one: DriverVer takes four numbers (v0.6.2-89-gfbedb26 gives 0.6.2.89, v0.6.2 gives 0.6.2.0)
WINDRIVER_VERSION_WORDS := $(subst -, ,$(subst ., ,$(patsubst v%,%,$(VERSION))))
WINDRIVER_VERSION := $(or $(word 1,$(WINDRIVER_VERSION_WORDS)),0).$(or $(word 2,$(WINDRIVER_VERSION_WORDS)),0).$(or $(word 3,$(WINDRIVER_VERSION_WORDS)),0).$(or $(word 4,$(WINDRIVER_VERSION_WORDS)),0)
