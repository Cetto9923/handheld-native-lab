#ifndef FFRS_INPUT_JNI_H
#define FFRS_INPUT_JNI_H
#include "probe_types.h"
#include <jni.h>
namespace ffrs {
InputInfo ReadInputSnapshot(JNIEnv* env, jobject snapshot);
}
#endif
