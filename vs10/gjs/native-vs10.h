/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __GJS_NATIVE_VS10_H__
#define __GJS_NATIVE_VS10_H__

#if !defined (__GJS_GJS_MODULE_H__) && !defined (GJS_COMPILATION)
#error "Only <gjs/gjs-module.h> can be included directly."
#endif

#include <gjs/native.h>

G_BEGIN_DECLS

#define GJS_REGISTER_NATIVE_MODULE_WITH_FLAGS_WIN32(module_id_string, module_func, flags) \
    static void                                 \
    register_native_module (void)                                            \
    {                                                                        \
        gjs_register_native_module(module_id_string, module_func, flags); \
    } \
    BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) \
    { \
    switch (ul_reason_for_call) \
    { \
        case DLL_PROCESS_ATTACH: \
            register_native_module (); \
            break; \
        case DLL_PROCESS_DETACH: \
            break; \
        case DLL_THREAD_ATTACH: \
            break; \
        case DLL_THREAD_DETACH: \
            break; \
    } \
    return TRUE; \
    }

#define GJS_REGISTER_NATIVE_MODULE_WIN32(module_id_string, module_func)           \
    GJS_REGISTER_NATIVE_MODULE_WITH_FLAGS_WIN32(module_id_string, module_func, 0)

G_END_DECLS

#endif  /* __GJS_NATIVE_VS10_H__ */
