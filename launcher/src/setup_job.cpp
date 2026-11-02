#include "setup_job.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <regex>
#include <system_error>
#include <thread>

#ifndef _WIN32
#include <signal.h>
#endif

#include "launcher_config.h"

namespace fs = std::filesystem;
namespace cfg = launcher_config;

namespace {

struct Job {
    SetupStatus status;
    float rangeFrom = -1.0f, rangeTo = -1.0f;   // ##progress P Q
    int steps = 0;                               // seen without any ##progress line
    bool sawProgress = false;
    std::string lastError;
};

std::mutex g_mutex;
Job g_jobs[kGameCount];
int g_running = -1;
SDL_Process* g_process = nullptr;
std::thread g_reader;
std::atomic<bool> g_readerDone{false};
std::atomic<bool> g_cancel{false};
bool g_finishedFlag = false;

double Now() { return SDL_GetTicks() / 1000.0; }

void HandleLine(Job& job, const std::string& line, std::ofstream& log) {
    log << line << '\n';
    static const std::regex ninja(R"(^\[(\d+)/(\d+)\])");
    std::smatch m;
    if (line.rfind("##progress", 0) == 0) {
        float a = -1.0f, b = -1.0f;
        const int n = std::sscanf(line.c_str() + 10, "%f %f", &a, &b);
        if (n >= 1) {
            job.sawProgress = true;
            job.status.progress = std::max(job.status.progress, std::clamp(a / 100.0f, 0.0f, 1.0f));
            job.rangeFrom = n == 2 ? a / 100.0f : -1.0f;
            job.rangeTo = n == 2 ? b / 100.0f : -1.0f;
        }
    } else if (line.rfind("==> ", 0) == 0) {
        job.status.step = line.substr(4);
        if (!job.sawProgress) {
            // A script without progress lines: creep along with each step.
            ++job.steps;
            job.status.progress = std::min(0.9f, 1.0f - 1.0f / (1.0f + job.steps * 0.15f));
        }
    } else if (std::regex_search(line, m, ninja) && job.rangeFrom >= 0.0f) {
        const float done = std::stof(m[1].str()), total = std::stof(m[2].str());
        if (total > 0) {
            const float p = job.rangeFrom + (job.rangeTo - job.rangeFrom) * std::min(1.0f, done / total);
            job.status.progress = std::max(job.status.progress, p);
        }
    } else if (line.rfind("Error: ", 0) == 0) {
        job.lastError = line.substr(7);
    }
}

void ReadOutput(SDL_Process* process, int index, fs::path logPath) {
    std::error_code ec;
    fs::create_directories(logPath.parent_path(), ec);
    std::ofstream log(logPath, std::ios::binary | std::ios::trunc);
    SDL_IOStream* out = SDL_GetProcessOutput(process);
    std::string pending;
    char buffer[4096];
    for (;;) {
        // The stream doesn't block: no data yet means wait a little, the end means the script exited.
        const size_t n = out ? SDL_ReadIO(out, buffer, sizeof buffer) : 0;
        if (n == 0) {
            if (out && SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY) {
                SDL_Delay(50);
                continue;
            }
            break;
        }
        pending.append(buffer, n);
        size_t start = 0;
        for (size_t nl; (nl = pending.find_first_of("\r\n", start)) != std::string::npos; start = nl + 1) {
            std::string line = pending.substr(start, nl - start);
            // PowerShell colours and progress records can carry escape codes; drop them.
            line.erase(std::remove(line.begin(), line.end(), '\x1b'), line.end());
            if (line.empty()) continue;
            std::lock_guard<std::mutex> lock(g_mutex);
            HandleLine(g_jobs[index], line, log);
        }
        pending.erase(0, start);
        log.flush();
    }
    int exitCode = -1;
    SDL_WaitProcess(process, true, &exitCode);
    std::lock_guard<std::mutex> lock(g_mutex);
    Job& job = g_jobs[index];
    job.status.finishedAt = Now();
    if (g_cancel) {
        job.status.state = SetupState::Cancelled;
        job.status.step = "Stopped.";
    } else if (exitCode == 0) {
        job.status.state = SetupState::Done;
        job.status.progress = 1.0f;
        job.status.step = "Ready to play.";
    } else {
        job.status.state = SetupState::Failed;
        job.status.step = job.lastError.empty() ? "The build stopped (exit code " + std::to_string(exitCode) + ")."
                                                : job.lastError;
    }
    log << "--- finished, exit code " << exitCode << '\n';
    g_readerDone = true;
}

// Stops the build and everything it started (compilers run as grandchildren).
void KillTree(SDL_Process* process) {
    const Sint64 pid = SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0);
#ifdef _WIN32
    if (pid > 0) {
        const std::string pidText = std::to_string(pid);
        const char* args[] = {"taskkill.exe", "/PID", pidText.c_str(), "/T", "/F", nullptr};
        if (SDL_Process* kill = SDL_CreateProcess(args, false)) {
            SDL_WaitProcess(kill, true, nullptr);
            SDL_DestroyProcess(kill);
        }
    }
#else
    if (pid > 0) {
        ::kill(-pid_t(pid), SIGTERM);   // the script runs in its own process group (setsid)
    }
#endif
    SDL_KillProcess(process, true);
}

bool StartJob(int index) {
    Job& job = g_jobs[index];
    const GameId id = GameAt(index);
    const fs::path script = SetupScript(id);
    if (script.empty()) {
        job.status.state = SetupState::Failed;
        job.status.step = "This launcher has no setup for " + std::string(Game(id).title) + " yet.";
        return false;
    }
    const std::string scriptText = cfg::PathToUtf8(script);
    const std::string discText = cfg::PathToUtf8(job.status.disc);
    const std::string outText = cfg::PathToUtf8(InstallDir(id));
    std::vector<const char*> args;
    SDL_Environment* env = SDL_CreateEnvironment(true);
#ifdef _WIN32
    args = {"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", scriptText.c_str(),
            "-Disc", discText.c_str(), "-Out", outText.c_str(), nullptr};
    const bool background = true;   // no console window
#else
    args = {"setsid", "bash", scriptText.c_str(), discText.c_str(), nullptr};
    SDL_SetEnvironmentVariable(env, "OUT", outText.c_str(), true);
    const bool background = false;  // background would detach it, and its output with it
#endif
    SDL_SetEnvironmentVariable(env, "ET_LAUNCHER_SETUP", "1", true);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, background);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (process == nullptr) {
        job.status.state = SetupState::Failed;
        job.status.step = std::string("Could not start the build: ") + SDL_GetError();
        return false;
    }
    job.status.state = SetupState::Running;
    job.status.progress = 0.0f;
    job.status.step = "Starting...";
    job.status.startedAt = Now();
    job.status.log = LauncherDataDir() / "logs" / (std::string(Game(id).key) + "-setup.txt");
    job.rangeFrom = job.rangeTo = -1.0f;
    job.steps = 0;
    job.sawProgress = false;
    job.lastError.clear();
    g_running = index;
    g_process = process;
    g_cancel = false;
    g_readerDone = false;
    g_reader = std::thread(ReadOutput, process, index, job.status.log);
    return true;
}

} // namespace

void SetupStart(GameId id, const fs::path& disc) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Job& job = g_jobs[int(id)];
    if (job.status.state == SetupState::Queued || job.status.state == SetupState::Running) return;
    job.status = SetupStatus{};
    job.status.state = SetupState::Queued;
    job.status.disc = disc;
    job.status.step = "Waiting for the other game to finish.";
}

void SetupCancel(GameId id) {
    const int index = int(id);
    std::unique_lock<std::mutex> lock(g_mutex);
    Job& job = g_jobs[index];
    if (job.status.state == SetupState::Queued) {
        job.status.state = SetupState::Cancelled;
        job.status.step = "Stopped.";
        return;
    }
    if (job.status.state == SetupState::Running && g_running == index && g_process) {
        g_cancel = true;
        job.status.step = "Stopping...";
        SDL_Process* process = g_process;
        lock.unlock();
        KillTree(process);
    }
}

SetupStatus SetupGet(GameId id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_jobs[int(id)].status;
}

bool SetupBusy() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Job& job : g_jobs) {
        if (job.status.state == SetupState::Queued || job.status.state == SetupState::Running) return true;
    }
    return false;
}

void SetupClear(GameId id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SetupStatus& s = g_jobs[int(id)].status;
    if (s.state != SetupState::Queued && s.state != SetupState::Running) {
        s = SetupStatus{};
    }
}

bool SetupPump() {
    bool finished = false;
    if (g_running >= 0 && g_readerDone) {
        g_reader.join();
        SDL_DestroyProcess(g_process);
        g_process = nullptr;
        g_running = -1;
        finished = true;
    }
    if (g_running < 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int i = 0; i < kGameCount; ++i) {
            if (g_jobs[i].status.state == SetupState::Queued) {
                StartJob(i);
                if (g_running >= 0) break;
                finished = true;   // it failed to start
            }
        }
    }
    return finished;
}

void SetupStopAll() {
    for (int i = 0; i < kGameCount; ++i) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_jobs[i].status.state == SetupState::Queued) g_jobs[i].status.state = SetupState::Cancelled;
    }
    if (g_running >= 0 && g_process) {
        g_cancel = true;
        KillTree(g_process);
        g_reader.join();
        SDL_DestroyProcess(g_process);
        g_process = nullptr;
        g_running = -1;
    }
}
