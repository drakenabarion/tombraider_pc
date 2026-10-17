// bbport: the parts of AnyPS5's libc/General.hpp that its libSceNgs2 uses.
#pragma once
#include <stdexcept>
#include <string>
// Guest calls use the System V ABI (the default on Linux).
#ifdef _WIN32
#define APS5_VABI __attribute__((sysv_abi))
#else
#define APS5_VABI
#endif
// Thrown here, turned into an NGS2 error code at the export (ngs2_register.cpp): a C++ exception
// must not reach guest code, which cannot unwind it.
#define APS5_INVALID_ARG_EX throw std::invalid_argument(std::string(__func__) + ": invalid argument")
extern "C" void NotImplemented_nid_no_patch(const char* funcName);
