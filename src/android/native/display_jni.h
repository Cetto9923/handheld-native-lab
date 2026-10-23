#ifndef FFRS_DISPLAY_JNI_H
#define FFRS_DISPLAY_JNI_H
#include "probe_types.h"
#include <jni.h>
namespace ffrs {
DisplayInfo ReadDisplaySnapshot(JNIEnv* env, jobject snapshot);
}
#endif
