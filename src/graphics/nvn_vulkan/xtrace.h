// 系统时间线打点：鸿蒙走 HiTrace，Android API 23+ 走 ATrace，其它平台为空。用来在系统时间线上看长帧时各线程在干活、在等，还是被抢占。
// System timeline markers: HiTrace on HarmonyOS, ATrace on Android API 23+, empty elsewhere. Used to see on the system timeline whether each thread is working, waiting or preempted during a long frame.
#pragma once
#ifdef __OHOS__
#include <hitrace/trace.h>
#define XTRACE_BEGIN(name) OH_HiTrace_StartTrace(name)
#define XTRACE_END() OH_HiTrace_FinishTrace()
#define XTRACE_COUNT(name, v) OH_HiTrace_CountTrace(name, (int64_t)(v))
#elif defined(__ANDROID__) && __ANDROID_API__ >= 23
#include <android/trace.h>
// Diagnostic synchronous slices only. Keep begin/end on the same thread.
// Counter API is API 29: deliberately unavailable in this API 23+ patch.
#define XTRACE_BEGIN(name) ATrace_beginSection(name)
#define XTRACE_END() ATrace_endSection()
#define XTRACE_COUNT(name, v) ((void)0)
#else
#define XTRACE_BEGIN(name) ((void)0)
#define XTRACE_END() ((void)0)
#define XTRACE_COUNT(name, v) ((void)0)
#endif
