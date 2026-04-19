#include "external/zstd/lib/zstd.h"
#include <jni.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/resource.h>
#include <sys/auxv.h>
#include "utils.h"