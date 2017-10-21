#include <stdlib.h>
#include <stdbool.h>
#include <malloc.h>
#include <unistd.h>
#include <jni.h>
#include "moar_syscall.h"
#include "linux_syscall_support.h"

#if I10N_DEBUG
#include <android/log.h>
#define LOG(...) ((void) __android_log_print(ANDROID_LOG_DEBUG, "i10n", __VA_ARGS__))
#define LOGE(...) ((void) __android_log_print(ANDROID_LOG_ERROR, "i10n", __VA_ARGS__))
#else
#define LOG(...) {}
#define LOGE(...) {}
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

const static int candidate_signals[] = { SIGWINCH, SIGTTIN, SIGTTOU };

static pid_t my_pid;
static uid_t my_uid;
static int chosen_signal;
static jclass isException;
static jmethodID constructor;

static __attribute__ ((noinline, cold)) void interruption_handler(int signo, siginfo_t* info, void* unused) {
    if (info->si_code == SI_QUEUE) {
        void *ref = info->si_value.sival_ptr;

        if (ref != NULL) {
            *((volatile _Atomic uint8_t*) ref) = 1;
        }
    }
}

static inline bool cacheRefs(JNIEnv *env, jclass wrapper) {
    isException = (*env) -> FindClass(env, "java/lang/IllegalStateException");
    if (isException == NULL) {
        (*env) -> FatalError(env, "unable to load IllegalStateException");
        return true;
    }

    isException = (*env) -> NewGlobalRef(env, isException);
    if (isException == NULL) {
        return true;
    }

    constructor = (*env) -> GetMethodID(env, wrapper, "<init>", "(JLjava/nio/ByteBuffer;)V");
    if (constructor == NULL) {
        return true;
    }

    return false;
}

static inline char* str_concat(const char* first, char* second) {
    size_t f = strlen(first);
    size_t s = strlen(second);

    size_t totalLen = f + s + 1;

    char* buf = malloc(totalLen);

    strcpy(buf, first);

    buf[f] = ' ';

    strcpy(buf + f + 1, second);

    return buf;
}

static void throwException(JNIEnv *env, const char* message) {
    char* msg = str_concat(message, strerror(errno));

    (*env) -> ThrowNew(env, isException, msg);

    free(msg);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    chosen_signal = 0;

    my_pid = getpid();
    my_uid = getuid();

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_net_sf_xfd_Interruption_i10nInit(JNIEnv *env, jclass type) {
    if (cacheRefs(env, type)) {
        return;
    }

    int candidate_signal = 0;

    struct kernel_sigaction prev_sigaction;

    for (int i = 0; i < ARRAY_SIZE(candidate_signals); ++i) {
        if (sys_rt_sigaction(candidate_signals[i], NULL, &prev_sigaction, sizeof(struct kernel_sigset_t))) {
            throwException(env, "Failed to probe for signal handlers");
            return;
        }

        if (prev_sigaction.sa_handler_ == SIG_DFL || prev_sigaction.sa_handler_ == SIG_IGN) {
            candidate_signal = candidate_signals[i];
            break;
        }
    }

    if (candidate_signal == 0) {
        throwException(env, "Failed to install signal handler, all busy");
        return;
    }

    LOG("Trying to hook onto %d", candidate_signal);

    struct kernel_sigaction new_handler = {};
    new_handler.sa_sigaction_ = interruption_handler;
    new_handler.sa_flags = SA_SIGINFO;

    if (sys_rt_sigaction(candidate_signal, &new_handler, NULL, sizeof(struct kernel_sigset_t))) {
        throwException(env, "Failed to install signal handler");
        return;
    }

    chosen_signal = candidate_signal;
}

JNIEXPORT void JNICALL Java_net_sf_xfd_Interruption_interrupt(JNIEnv *env, jclass type, jlong curlPtr, jint tid) {
    if (!chosen_signal) return;

    LOG("Sending %d to %d", chosen_signal, tid);

    volatile _Atomic uint8_t* ctrl = (volatile _Atomic uint8_t*) (intptr_t) curlPtr;

    union sigval val;
    val.sival_ptr = (void *) ctrl;

    siginfo_t info;
    memset(&info, 0, sizeof(siginfo_t));
    info.si_signo = chosen_signal;
    info.si_code = SI_QUEUE;
    info.si_pid = my_pid;
    info.si_uid = my_uid;
    info.si_value = val;

    if (sys_rt_tgsigqueueinfo(my_pid, tid, chosen_signal, &info)) {
        if (errno == ESRCH) {
            LOGE("Failed to interrupt TID %d; already dead?", tid);
        } else {
            throwException(env, "Failed to dispatch interruption signal");
        }
    }
}

JNIEXPORT jobject JNICALL Java_net_sf_xfd_Interruption_newInstance(JNIEnv *env, jclass type) {
    if (chosen_signal) {
        struct kernel_sigset_t new_set;

        sys_sigemptyset(&new_set);
        sys_sigaddset(&new_set, chosen_signal);

        if (sys_rt_sigprocmask(SIG_UNBLOCK, &new_set, &new_set, sizeof(struct kernel_sigset_t))) {
            throwException(env, "Failed to unblock signal");
            return NULL;
        }
    }

    void* flag = malloc(sizeof(uint8_t));

    jobject buffer = (*env) -> NewDirectByteBuffer(env, flag, sizeof(uint8_t));
    if (buffer == NULL) {
        return NULL;
    }

    return (*env) -> NewObject(env, type, constructor, (jlong) (intptr_t) flag, buffer);
}

JNIEXPORT void JNICALL Java_net_sf_xfd_Interruption_destroy(JNIEnv *env, jclass type, jlong ptr) {
    free((void*) (intptr_t) ptr);
}