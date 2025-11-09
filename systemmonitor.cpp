// system_monitor.cpp
// Single-file system monitor (top-like) for Linux (WSL/Ubuntu).
// Compile: g++ -std=c++17 -O2 system_monitor.cpp -lncurses -o system_monitor
// Run:   ./system_monitor    (run inside WSL/Ubuntu)

#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iomanip>
#include <iostream>

using namespace std::chrono;

struct Proc {
    int pid = 0;
    std::string name;
    unsigned long long prev_time = 0; // in clock ticks
    unsigned long long time = 0;      // current time in clock ticks
    long rss_pages = 0;               // resident set size (pages)
    double cpu_pct = 0.0;
    double mem_pct = 0.0;
};

static long CLK_TCK = sysconf(_SC_CLK_TCK);
static long PAGE_SIZE = sysconf(_SC_PAGESIZE);

unsigned long long read_total_time_from_proc_stat() {
    std::ifstream f("/proc/stat");
    std::string line;
    if (!std::getline(f, line)) return 0;
    std::istringstream iss(line);
    std::string cpu;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    user = nice = system = idle = iowait = irq = softirq = steal = 0;
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

double get_uptime_seconds() {
    std::ifstream f("/proc/uptime");
    double up = 0;
    f >> up;
    return up;
}

void read_mem_info(double &total_mb, double &free_mb, double &avail_mb) {
    std::ifstream f("/proc/meminfo");
    std::string key;
    unsigned long value;
    std::string unit;
    total_mb = free_mb = avail_mb = 0.0;
    while (f >> key >> value >> unit) {
        if (key == "MemTotal:") total_mb = value / 1024.0;
        else if (key == "MemFree:") free_mb = value / 1024.0;
        else if (key == "MemAvailable:") avail_mb = value / 1024.0;
    }
}

std::string read_first_line(const std::string &path) {
    std::ifstream f(path);
    std::string s;
    if (std::getline(f, s)) return s;
    return "";
}

bool is_digits(const char* s) {
    if (!s || !*s) return false;
    while (*s) {
        if (!isdigit(*s)) return false;
        ++s;
    }
    return true;
}

Proc read_process_basic(int pid) {
    Proc p;
    p.pid = pid;

    // name from /proc/<pid>/comm
    p.name = read_first_line("/proc/" + std::to_string(pid) + "/comm");

    // stat file for utime(14) stime(15) cutime cstime starttime(22)
    std::string stat = read_first_line("/proc/" + std::to_string(pid) + "/stat");
    if (!stat.empty()) {
        std::istringstream iss(stat);
        std::string token;
        // Fields: pid(1) comm(2) state(3) ... utime is 14th, stime 15th
        // We'll parse tokens up to 22
        std::vector<std::string> toks;
        while (iss >> token) toks.push_back(token);
        if (toks.size() >= 22) {
            unsigned long long utime = std::stoull(toks[13]);
            unsigned long long stime = std::stoull(toks[14]);
            unsigned long long total_time = utime + stime;
            p.time = total_time;
        }
    }

    // rss from statm or status
    std::string statm = read_first_line("/proc/" + std::to_string(pid) + "/statm");
    if (!statm.empty()) {
        std::istringstream iss(statm);
        long rss = 0;
        long size = 0;
        iss >> size >> rss;
        p.rss_pages = rss; // pages
    } else {
        // fallback: parse VmRSS in /proc/<pid>/status
        std::ifstream st("/proc/" + std::to_string(pid) + "/status");
        std::string line;
        while (std::getline(st, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream iss(line);
                std::string k;
                long kb;
                iss >> k >> kb;
                p.rss_pages = kb * 1024 / PAGE_SIZE;
                break;
            }
        }
    }

    return p;
}

std::vector<Proc> get_all_processes() {
    std::vector<Proc> procs;
    DIR *d = opendir("/proc");
    if (!d) return procs;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (is_digits(entry->d_name)) {
            int pid = atoi(entry->d_name);
            // try reading limited info; many pids might vanish between reads
            Proc p = read_process_basic(pid);
            procs.push_back(std::move(p));
        }
    }
    closedir(d);
    return procs;
}

void draw_bar(int y, int x, int width, double fraction) {
    int filled = (int)(fraction * width + 0.5);
    for (int i = 0; i < width; ++i) {
        mvaddch(y, x + i, i < filled ? ACS_CKBOARD : ' ');
    }
}

// Main program
int main() {
    // Init ncurses
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // bookkeeping
    std::map<int, unsigned long long> prev_proc_time; // pid -> clock ticks
    unsigned long long prev_total_time = read_total_time_from_proc_stat();
    auto last_time = steady_clock::now();

    // sorting mode: 0 = CPU desc, 1 = MEM desc, 2 = PID asc
    int sort_mode = 0;

    while (true) {
        // handle resize
        getmaxyx(stdscr, rows, cols);

        // sample times
        auto now = steady_clock::now();
        double interval = duration_cast<duration<double>>(now - last_time).count();
        if (interval <= 0.0) interval = 1.0; // fallback
        last_time = now;

        unsigned long long total_time = read_total_time_from_proc_stat();
        unsigned long long total_time_delta = (total_time > prev_total_time) ? (total_time - prev_total_time) : 0ULL;
        prev_total_time = total_time;

        double uptime = get_uptime_seconds();

        double mem_total_mb, mem_free_mb, mem_avail_mb;
        read_mem_info(mem_total_mb, mem_free_mb, mem_avail_mb);

        // Read processes
        std::vector<Proc> procs = get_all_processes();

        // Compute per-process deltas and percentages
        for (auto &p : procs) {
            unsigned long long prev = prev_proc_time.count(p.pid) ? prev_proc_time[p.pid] : p.time;
            unsigned long long delta = (p.time > prev) ? (p.time - prev) : 0ULL;
            // CPU percent = (proc_time_delta / CLK_TCK) / interval * 100
            double proc_seconds = (double)delta / (double)CLK_TCK;
            p.cpu_pct = (interval > 0.0) ? (proc_seconds / interval) * 100.0 : 0.0;

            prev_proc_time[p.pid] = p.time;

            // memory percent
            double rss_bytes = (double)p.rss_pages * (double)PAGE_SIZE;
            double rss_mb = rss_bytes / (1024.0 * 1024.0);
            p.mem_pct = (mem_total_mb > 0.0) ? (rss_mb / mem_total_mb) * 100.0 : 0.0;
        }

        // sort
        if (sort_mode == 0) {
            std::sort(procs.begin(), procs.end(), [](const Proc &a, const Proc &b) {
                if (a.cpu_pct == b.cpu_pct) return a.pid < b.pid;
                return a.cpu_pct > b.cpu_pct;
            });
        } else if (sort_mode == 1) {
            std::sort(procs.begin(), procs.end(), [](const Proc &a, const Proc &b) {
                if (a.mem_pct == b.mem_pct) return a.pid < b.pid;
                return a.mem_pct > b.mem_pct;
            });
        } else {
            std::sort(procs.begin(), procs.end(), [](const Proc &a, const Proc &b) {
                return a.pid < b.pid;
            });
        }

        // UI
        clear();
        // Header
        mvprintw(0, 0, "Simple System Monitor (single-file)  —  q:quit  k:kill  s:sort-mode");
        mvprintw(1, 0, "Sort: %s", (sort_mode == 0 ? "CPU %" : (sort_mode == 1 ? "MEM %" : "PID")));
        // CPU overall (approx using /proc/stat)
        double cpu_pct = 0.0;
        if (total_time_delta > 0) {
            // busy = total_time_delta - idle_delta? We didn't track idle separately; estimate using previous total only gives proportion of all jiffies.
            // Simpler approach: show 100 * (busy_jiffies / total_jiffies). We'll re-read /proc/stat to get idle if desired;
            // For simplicity display "N CPUs" and approximate overall using sum of process cpu over interval (may be < 100 for multi-core).
            double sum_proc_cpu = 0.0;
            for (auto &p : procs) sum_proc_cpu += p.cpu_pct;
            cpu_pct = sum_proc_cpu; // note: sum could be >100 if many processes; it's a rough indicator
        }
        mvprintw(2, 0, "Uptime: %.1fs  CPU (sum processes): %.2f%%  Mem: %.1fMB total  Avail: %.1fMB",
                 uptime, cpu_pct, mem_total_mb, mem_avail_mb);

        // visual bars
        int bar_y = 3;
        int bar_w = std::max(20, cols / 3);
        mvprintw(bar_y, 0, "CPU bar (sum processes):");
        double cpu_fraction = std::min(1.0, cpu_pct / 100.0);
        draw_bar(bar_y, 24, bar_w, cpu_fraction);

        mvprintw(bar_y + 1, 0, "Memory usage:");
        double used_mem_mb = mem_total_mb - mem_avail_mb;
        double mem_fraction = mem_total_mb > 0 ? (used_mem_mb / mem_total_mb) : 0.0;
        draw_bar(bar_y + 1, 24, bar_w, mem_fraction);
        mvprintw(bar_y + 1, 24 + bar_w + 2, "%.1f/%.1fMB (%.1f%%)", used_mem_mb, mem_total_mb, mem_fraction * 100.0);

        // Table header
        int row = bar_y + 3;
        mvprintw(row++, 0, "%-6s %-20s %8s %8s", "PID", "NAME", "CPU %", "MEM %");

        // show top N processes that fit on screen
        int max_rows = rows - row - 2;
        if (max_rows < 1) max_rows = 1;
        int shown = 0;
        for (auto &p : procs) {
            if (shown >= max_rows) break;
            // sanitize name length
            std::string name = p.name.empty() ? "[" + std::to_string(p.pid) + "]" : p.name;
            if ((int)name.size() > 20) name = name.substr(0, 17) + "...";

            mvprintw(row + shown, 0, "%-6d %-20s %8.2f %8.2f", p.pid, name.c_str(), p.cpu_pct, p.mem_pct);
            ++shown;
        }

        mvprintw(rows - 2, 0, "Commands: q=quit  s=toggle sort (CPU/MEM/PID)  k=kill <pid>");
        mvprintw(rows - 1, 0, "Enter command: ");

        // refresh
        refresh();

        // input handling (non-blocking)
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == 's' || ch == 'S') {
            sort_mode = (sort_mode + 1) % 3;
        } else if (ch == 'k' || ch == 'K') {
            // prompt for pid. switch to blocking input
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(rows - 1, 0, "Enter PID to kill: ");
            char buf[32];
            getnstr(buf, sizeof(buf)-1);
            int okpid = atoi(buf);
            if (okpid > 0) {
                int res = kill(okpid, SIGTERM);
                if (res == 0) {
                    mvprintw(rows - 1, 0, "Sent SIGTERM to %d. Press any key to continue...", okpid);
                } else {
                    mvprintw(rows - 1, 0, "Failed to kill %d (check permissions). Press any key to continue...", okpid);
                }
                refresh();
                getch();
            }
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
        } else {
            // sleep small interval
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // wait until ~1 second elapsed since last sample
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    endwin();
    return 0;
}