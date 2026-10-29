#ifndef VVD_CORE_WINCOMPAT_H
#define VVD_CORE_WINCOMPAT_H

// <windows.h> defines Yield() as an empty function-like macro (winbase.h) and
// MessageBox as MessageBoxW/MessageBoxA (winuser.h). Both collide with engine
// names (vvd::Thread::Yield, vvd::UIBackend::Yield, vvd::UI::MessageBox):
// "static void Yield() {...}" becomes "static void  {...}" (C2334/C2062), and
// MessageBox is renamed only in translation units that happened to include
// <windows.h> first. Include <windows.h> here once (its include guards make
// later includes no-ops) and drop the two macros, the same thing
// wxWidgets' wx/msw/winundef.h does. Call the Win32 API as MessageBoxW/A.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// compatibility.h includes <ole2.h>; most engine headers reach it after their
// own "using namespace std". Parse the COM headers here, first, so the SDK's
// ::byte (rpcndr.h) is declared before std::byte is visible unqualified (the
// Qt app enables std::byte for Qt 6).
#include <ole2.h>
#ifdef Yield
#undef Yield
#endif
#ifdef MessageBox
#undef MessageBox
#endif
#endif

#endif // VVD_CORE_WINCOMPAT_H
