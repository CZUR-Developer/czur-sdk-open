// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFDIR) == _S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFREG) == _S_IFREG)
#endif
#endif
