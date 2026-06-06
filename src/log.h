#ifndef DS5_BRIDGE_LOG_H
#define DS5_BRIDGE_LOG_H

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <chrono>

// Global log file handle — defined in main.cpp
extern FILE *g_logfile;

// Initialize persistent logging to file.
// Opens the file in write mode (truncates). Line-buffered so each
// log line is flushed immediately, preventing data loss on crash.
inline void log_init(const char *path) {
    g_logfile = fopen(path, "w");
    if (g_logfile) {
        setvbuf(g_logfile, nullptr, _IOLBF, 0); // line-buffered
    } else {
        fprintf(stderr, "[LOG] ⚠️ Falha ao abrir %s para escrita: log somente no terminal\n", path);
    }
}

inline void log_close() {
    if (g_logfile) {
        fclose(g_logfile);
        g_logfile = nullptr;
    }
}

// blog() — Bridge Log: writes to both stdout and the log file.
// Uses the same format as printf(). Thread-safe as long as stdio is
// thread-safe (which it is on glibc with line-buffered streams).
__attribute__((format(printf, 1, 2)))
inline void blog(const char *fmt, ...) {
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    vprintf(fmt, args);
    if (g_logfile) vfprintf(g_logfile, fmt, args2);
    va_end(args2);
    va_end(args);
}

// tlog() — Timestamped Bridge Log: prepends [HH:MM:SS.mmm] to the message.
// Useful for measuring latency between output reports.
__attribute__((format(printf, 1, 2)))
inline void tlog(const char *fmt, ...) {
    // Get current time with millisecond precision
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);

    char tbuf[20];
    snprintf(tbuf, sizeof(tbuf), "[%02d:%02d:%02d.%03d] ",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms);

    // Print timestamp + formatted message to both outputs
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);

    printf("%s", tbuf);
    vprintf(fmt, args);

    if (g_logfile) {
        fprintf(g_logfile, "%s", tbuf);
        vfprintf(g_logfile, fmt, args2);
    }

    va_end(args2);
    va_end(args);
}

#endif // DS5_BRIDGE_LOG_H
