#include "NativeDebug.h"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>

namespace NativeDebug {
namespace {

constexpr char kLogTag[] = "PolywarDebug";
constexpr size_t kPathCapacity = 768;
constexpr off_t kMaximumPreviousLogSize = 8 * 1024 * 1024;

std::atomic<int> g_log_fd(-1);
std::atomic<int32_t> g_stage(static_cast<int32_t>(CrashStage::Startup));
volatile sig_atomic_t g_last_touch_count = -1;
volatile sig_atomic_t g_last_touch_phase = -1;
volatile sig_atomic_t g_last_touch_x100 = -1;
volatile sig_atomic_t g_last_touch_y100 = -1;
volatile sig_atomic_t g_last_mouse_down = 0;
volatile sig_atomic_t g_handling_signal = 0;
char g_log_path[kPathCapacity] = {};
char g_package_name[256] = {};

constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
struct sigaction g_previous_actions[sizeof(kSignals) / sizeof(kSignals[0])] = {};
bool g_previous_action_valid[sizeof(kSignals) / sizeof(kSignals[0])] = {};

pid_t GetThreadId() {
    return static_cast<pid_t>(syscall(SYS_gettid));
}

bool IsUsablePackageName(const char *name) {
    if (!name || !*name)
        return false;
    for (const char *cursor = name; *cursor; ++cursor) {
        const char value = *cursor;
        const bool valid = (value >= 'a' && value <= 'z') ||
                           (value >= 'A' && value <= 'Z') ||
                           (value >= '0' && value <= '9') || value == '.' ||
                           value == '_';
        if (!valid)
            return false;
    }
    return true;
}

void DetectPackageName(const char *fallback_package_name) {
    char process_name[sizeof(g_package_name)] = {};
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        const ssize_t length = read(fd, process_name, sizeof(process_name) - 1);
        close(fd);
        if (length > 0) {
            process_name[length] = '\0';
            char *suffix = strchr(process_name, ':');
            if (suffix)
                *suffix = '\0';
        }
    }

    const char *selected = IsUsablePackageName(process_name)
                               ? process_name
                               : fallback_package_name;
    if (!IsUsablePackageName(selected))
        selected = "com.Nobodyshot.kuboom";
    snprintf(g_package_name, sizeof(g_package_name), "%s", selected);
}

bool EnsureDirectory(const char *path) {
    if (!path || path[0] != '/')
        return false;

    char current[kPathCapacity] = {};
    const size_t length = strnlen(path, sizeof(current));
    if (length == 0 || length >= sizeof(current))
        return false;
    memcpy(current, path, length + 1);

    for (char *cursor = current + 1; *cursor; ++cursor) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(current, 0755) != 0 && errno != EEXIST) {
            *cursor = '/';
            return false;
        }
        *cursor = '/';
    }
    return mkdir(current, 0755) == 0 || errno == EEXIST;
}

int OpenLogInDirectory(const char *directory) {
    if (!EnsureDirectory(directory))
        return -1;

    char path[kPathCapacity] = {};
    if (snprintf(path, sizeof(path), "%s/polywar_debug.txt", directory) >=
        static_cast<int>(sizeof(path))) {
        return -1;
    }

    struct stat status {};
    if (stat(path, &status) == 0 && status.st_size > kMaximumPreviousLogSize) {
        char previous[kPathCapacity] = {};
        snprintf(previous, sizeof(previous), "%s/polywar_debug.previous.txt",
                 directory);
        unlink(previous);
        rename(path, previous);
    }

    const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;

    snprintf(g_log_path, sizeof(g_log_path), "%s", path);
    return fd;
}

bool SelectLogDirectory(const char *directory) {
    const int current_fd = g_log_fd.load(std::memory_order_acquire);
    if (current_fd >= 0)
        return true;

    const int new_fd = OpenLogInDirectory(directory);
    if (new_fd < 0)
        return false;

    int expected = -1;
    if (!g_log_fd.compare_exchange_strong(expected, new_fd,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
        close(new_fd);
    }
    return true;
}

void WriteAll(int fd, const char *data, size_t length) {
    while (fd >= 0 && data && length > 0) {
        const ssize_t written = write(fd, data, length);
        if (written > 0) {
            data += written;
            length -= static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        break;
    }
}

const char *StageName(int32_t stage) {
    switch (static_cast<CrashStage>(stage)) {
        case CrashStage::Startup: return "Startup";
        case CrashStage::EglSwapEntered: return "EglSwapEntered";
        case CrashStage::AndroidNewFrame: return "AndroidNewFrame";
        case CrashStage::TouchCountCall: return "TouchCountCall";
        case CrashStage::TouchReadCall: return "TouchReadCall";
        case CrashStage::TouchApply: return "TouchApply";
        case CrashStage::OpenGlNewFrame: return "OpenGlNewFrame";
        case CrashStage::ImGuiNewFrame: return "ImGuiNewFrame";
        case CrashStage::DrawMenu: return "DrawMenu";
        case CrashStage::DrawEsp: return "DrawEsp";
        case CrashStage::ImGuiRender: return "ImGuiRender";
        case CrashStage::RenderDrawData: return "RenderDrawData";
        case CrashStage::SwapOriginal: return "SwapOriginal";
        case CrashStage::Idle: return "Idle";
    }
    return "Unknown";
}

const char *SignalName(int signal_number) {
    switch (signal_number) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        default: return "UNKNOWN";
    }
}

class SignalBuffer {
public:
    void Append(const char *value) {
        if (!value)
            return;
        while (*value && length_ + 1 < sizeof(data_))
            data_[length_++] = *value++;
    }

    void AppendDecimal(int64_t value) {
        if (value < 0) {
            Append("-");
            AppendUnsigned(static_cast<uint64_t>(-(value + 1)) + 1);
            return;
        }
        AppendUnsigned(static_cast<uint64_t>(value));
    }

    void AppendHex(uint64_t value) {
        static constexpr char digits[] = "0123456789abcdef";
        Append("0x");
        char reversed[16];
        size_t count = 0;
        do {
            reversed[count++] = digits[value & 0xfu];
            value >>= 4u;
        } while (value && count < sizeof(reversed));
        while (count > 0 && length_ + 1 < sizeof(data_))
            data_[length_++] = reversed[--count];
    }

    const char *Data() const { return data_; }
    size_t Size() const { return length_; }

private:
    void AppendUnsigned(uint64_t value) {
        char reversed[24];
        size_t count = 0;
        do {
            reversed[count++] = static_cast<char>('0' + value % 10u);
            value /= 10u;
        } while (value && count < sizeof(reversed));
        while (count > 0 && length_ + 1 < sizeof(data_))
            data_[length_++] = reversed[--count];
    }

    char data_[8192] = {};
    size_t length_ = 0;
};

void AppendProcFile(int log_fd, const char *title, const char *path) {
    WriteAll(log_fd, title, strlen(title));
    const int source = open(path, O_RDONLY | O_CLOEXEC);
    if (source < 0) {
        static constexpr char unavailable[] = "<unavailable>\n";
        WriteAll(log_fd, unavailable, sizeof(unavailable) - 1);
        return;
    }

    char buffer[4096];
    while (true) {
        const ssize_t count = read(source, buffer, sizeof(buffer));
        if (count > 0) {
            WriteAll(log_fd, buffer, static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
    close(source);
}

int SignalIndex(int signal_number) {
    for (size_t index = 0; index < sizeof(kSignals) / sizeof(kSignals[0]);
         ++index) {
        if (kSignals[index] == signal_number)
            return static_cast<int>(index);
    }
    return -1;
}

void CrashSignalHandler(int signal_number, siginfo_t *info, void *context_ptr) {
    if (g_handling_signal) {
        _exit(128 + signal_number);
    }
    g_handling_signal = 1;

    const int log_fd = g_log_fd.load(std::memory_order_relaxed);
    SignalBuffer report;
    report.Append("\n========== POLYWAR NATIVE CRASH ==========\n");
    report.Append("epoch_seconds=");
    struct timespec now {};
    if (clock_gettime(CLOCK_REALTIME, &now) == 0)
        report.AppendDecimal(now.tv_sec);
    else
        report.Append("unavailable");
    report.Append("\npid=");
    report.AppendDecimal(getpid());
    report.Append(" tid=");
    report.AppendDecimal(GetThreadId());
    report.Append("\nsignal=");
    report.Append(SignalName(signal_number));
    report.Append(" (");
    report.AppendDecimal(signal_number);
    report.Append(") code=");
    report.AppendDecimal(info ? info->si_code : 0);
    report.Append(" fault_address=");
    report.AppendHex(reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
    report.Append("\nlast_stage=");
    report.Append(StageName(g_stage.load(std::memory_order_relaxed)));
    report.Append("\nlast_touch_count=");
    report.AppendDecimal(g_last_touch_count);
    report.Append(" phase=");
    report.AppendDecimal(g_last_touch_phase);
    report.Append(" x100=");
    report.AppendDecimal(g_last_touch_x100);
    report.Append(" y100=");
    report.AppendDecimal(g_last_touch_y100);
    report.Append(" mouse_down=");
    report.AppendDecimal(g_last_mouse_down);
    report.Append("\n");

#if defined(__aarch64__)
    if (context_ptr) {
        const auto *context = reinterpret_cast<const ucontext_t *>(context_ptr);
        report.Append("pc=");
        report.AppendHex(context->uc_mcontext.pc);
        report.Append(" sp=");
        report.AppendHex(context->uc_mcontext.sp);
        report.Append(" lr=");
        report.AppendHex(context->uc_mcontext.regs[30]);
        report.Append(" pstate=");
        report.AppendHex(context->uc_mcontext.pstate);
        report.Append("\nregisters:\n");
        for (int index = 0; index < 31; ++index) {
            report.Append("x");
            report.AppendDecimal(index);
            report.Append("=");
            report.AppendHex(context->uc_mcontext.regs[index]);
            report.Append(index % 3 == 2 ? "\n" : " ");
        }
        report.Append("\n");
    }
#endif

    WriteAll(log_fd, report.Data(), report.Size());
    AppendProcFile(log_fd, "\n----- /proc/self/status -----\n",
                   "/proc/self/status");
    AppendProcFile(log_fd, "\n----- /proc/self/maps -----\n", "/proc/self/maps");
    static constexpr char footer[] =
        "\n========== END POLYWAR NATIVE CRASH ==========\n";
    WriteAll(log_fd, footer, sizeof(footer) - 1);
    if (log_fd >= 0)
        fsync(log_fd);

    const int index = SignalIndex(signal_number);
    if (index >= 0 && g_previous_action_valid[index])
        sigaction(signal_number, &g_previous_actions[index], nullptr);
    else
        signal(signal_number, SIG_DFL);

    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal_number);
    sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    syscall(SYS_tgkill, getpid(), GetThreadId(), signal_number);
    _exit(128 + signal_number);
}

jobject GetCurrentApplication(JNIEnv *env) {
    if (!env)
        return nullptr;
    jclass activity_thread = env->FindClass("android/app/ActivityThread");
    if (!activity_thread) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    const jmethodID current_application = env->GetStaticMethodID(
        activity_thread, "currentApplication", "()Landroid/app/Application;");
    if (!current_application) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }
    jobject application =
        env->CallStaticObjectMethod(activity_thread, current_application);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        application = nullptr;
    }
    env->DeleteLocalRef(activity_thread);
    return application;
}

bool SelectJavaDirectory(JNIEnv *env, jobject context, const char *method_name,
                         const char *signature, bool takes_null_argument) {
    if (!env || !context)
        return false;
    jclass context_class = env->GetObjectClass(context);
    if (!context_class)
        return false;
    const jmethodID get_directory =
        env->GetMethodID(context_class, method_name, signature);
    if (!get_directory) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(context_class);
        return false;
    }
    jobject file = takes_null_argument
                       ? env->CallObjectMethod(context, get_directory, nullptr)
                       : env->CallObjectMethod(context, get_directory);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        file = nullptr;
    }
    env->DeleteLocalRef(context_class);
    if (!file)
        return false;

    jclass file_class = env->FindClass("java/io/File");
    const jmethodID absolute_path = file_class
                                        ? env->GetMethodID(
                                              file_class, "getAbsolutePath",
                                              "()Ljava/lang/String;")
                                        : nullptr;
    jstring path = absolute_path
                       ? static_cast<jstring>(
                             env->CallObjectMethod(file, absolute_path))
                       : nullptr;
    bool selected = false;
    if (path && !env->ExceptionCheck()) {
        const char *utf_path = env->GetStringUTFChars(path, nullptr);
        if (utf_path) {
            char directory[kPathCapacity] = {};
            if (snprintf(directory, sizeof(directory), "%s/PolywarDebug",
                         utf_path) < static_cast<int>(sizeof(directory))) {
                selected = SelectLogDirectory(directory);
            }
            env->ReleaseStringUTFChars(path, utf_path);
        }
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (path) env->DeleteLocalRef(path);
    if (file_class) env->DeleteLocalRef(file_class);
    env->DeleteLocalRef(file);
    return selected;
}

}  // namespace

void Initialize(const char *fallback_package_name) {
    if (g_log_fd.load(std::memory_order_acquire) >= 0)
        return;
    DetectPackageName(fallback_package_name);

    char directory[kPathCapacity] = {};
    const char *patterns[] = {
        "/storage/emulated/0/Android/data/%s/files/PolywarDebug",
        "/sdcard/Android/data/%s/files/PolywarDebug",
        "/data/user/0/%s/files/PolywarDebug",
        "/data/data/%s/files/PolywarDebug",
    };
    for (const char *pattern : patterns) {
        if (snprintf(directory, sizeof(directory), pattern, g_package_name) >=
            static_cast<int>(sizeof(directory))) {
            continue;
        }
        if (SelectLogDirectory(directory))
            break;
    }

    Log("session_start build=%s_%s pid=%d tid=%d package=%s path=%s",
        __DATE__, __TIME__, getpid(), GetThreadId(), g_package_name,
        g_log_path[0] ? g_log_path : "logcat-only");
}

void InitializeFromJava(JNIEnv *env) {
    if (g_log_fd.load(std::memory_order_acquire) >= 0 || !env)
        return;
    jobject application = GetCurrentApplication(env);
    if (!application)
        return;
    bool selected = SelectJavaDirectory(
        env, application, "getExternalFilesDir",
        "(Ljava/lang/String;)Ljava/io/File;", true);
    if (!selected) {
        SelectJavaDirectory(env, application, "getFilesDir", "()Ljava/io/File;",
                            false);
    }
    env->DeleteLocalRef(application);
    if (g_log_fd.load(std::memory_order_acquire) >= 0)
        Log("java_storage_ready path=%s", g_log_path);
}

void InstallCrashHandlers() {
    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = CrashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;

    for (size_t index = 0; index < sizeof(kSignals) / sizeof(kSignals[0]);
         ++index) {
        struct sigaction current {};
        if (sigaction(kSignals[index], nullptr, &current) != 0)
            continue;
        if ((current.sa_flags & SA_SIGINFO) &&
            current.sa_sigaction == CrashSignalHandler) {
            continue;
        }
        g_previous_actions[index] = current;
        g_previous_action_valid[index] = true;
        sigaction(kSignals[index], &action, nullptr);
    }
    Log("fatal_signal_handlers_installed");
}

void Log(const char *format, ...) {
    if (!format)
        return;

    char message[3072] = {};
    va_list arguments;
    va_start(arguments, format);
    va_list logcat_arguments;
    va_copy(logcat_arguments, arguments);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    __android_log_vprint(ANDROID_LOG_INFO, kLogTag, format, logcat_arguments);
    va_end(logcat_arguments);

    char line[3584] = {};
    struct timespec now {};
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm local_time {};
    localtime_r(&now.tv_sec, &local_time);
    const int prefix_length = snprintf(
        line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d.%03ld pid=%d tid=%d "
                            "stage=%s | ",
        local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
        local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
        now.tv_nsec / 1000000L, getpid(), GetThreadId(),
        StageName(g_stage.load(std::memory_order_relaxed)));
    if (prefix_length < 0 || prefix_length >= static_cast<int>(sizeof(line)))
        return;
    const size_t used = static_cast<size_t>(prefix_length);
    const int message_length = snprintf(line + used, sizeof(line) - used,
                                        "%s\n", message);
    if (message_length < 0)
        return;
    const size_t total = used +
        static_cast<size_t>(message_length >= static_cast<int>(sizeof(line) - used)
                                ? sizeof(line) - used - 1
                                : message_length);
    WriteAll(g_log_fd.load(std::memory_order_acquire), line, total);
}

void SetStage(CrashStage stage) {
    g_stage.store(static_cast<int32_t>(stage), std::memory_order_relaxed);
}

void SetTouchState(int32_t count, int32_t phase, float x, float y,
                   bool mouse_down) {
    g_last_touch_count = count;
    g_last_touch_phase = phase;
    g_last_touch_x100 = static_cast<sig_atomic_t>(x * 100.0f);
    g_last_touch_y100 = static_cast<sig_atomic_t>(y * 100.0f);
    g_last_mouse_down = mouse_down ? 1 : 0;
}

const char *GetLogPath() {
    return g_log_path[0] ? g_log_path : "logcat-only";
}

}  // namespace NativeDebug
