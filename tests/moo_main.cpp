// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The main of Moo.Test: moo's own tests and the determinism suite, linked against the VM and the
// compiler only, so the push gate builds in well under a minute even when a VM header changes.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
