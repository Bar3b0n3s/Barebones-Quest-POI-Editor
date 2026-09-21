#pragma once

#if defined(_WIN32)
#define QPE_CALL __stdcall
#else
#define QPE_CALL
#endif
