#include "mpc_plugin.h"

#include <atomic>
#include <chrono>
#include <thread>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

class TemplateChooser : public mpc::Plugin
{
public:
    TemplateChooser() : mpc::Plugin(makeInfo())
    {
        addControl(mpc::boolParam("reserved", "Reserved", false));
        addControl(mpc::choiceParam("action", "Action", {"Refresh", "Enable", "Restore"}, 0));
        addControl(mpc::boolParam("apply", "Apply", false));
        addControl(mpc::choiceParam("result", "Result",
            {"Ready", "Working", "Updated", "Enabled", "Restored", "Empty", "No reload", "Failed"}, 0));
#if defined(__linux__)
        worker_ = std::thread([this] { runWorker(); });
#endif
    }

    ~TemplateChooser() override
    {
        stopping_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    void process(mpc::ProcessContext& context) override
    {
        for (int channel = 0; channel < context.numOutputs; ++channel)
        {
            const float* input = channel < context.numInputs ? context.inputs[channel] : nullptr;
            float* output = context.outputs[channel];
            if (output && output != input)
            {
                if (input)
                    std::copy_n(input, context.numFrames, output);
                else
                    std::fill_n(output, context.numFrames, 0.0f);
            }
        }
    }

    void parameterChanged(int index, float value) override
    {
#if defined(__linux__)
        if (index == kApply && value >= 0.5f)
        {
            // Momentary control. The saved project stores the released state.
            set(kApply, 0);
            int idle = -1;
            request_.compare_exchange_strong(idle, getInt(kAction));
        }
#else
        (void)index;
        (void)value;
#endif
    }

private:
    enum Parameters { kReserved, kAction, kApply, kResult };
    enum Result { Ready, Working, Updated, Enabled, Restored, Empty, NoReload, Failed };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "MPC Template Chooser";
        info.vendor = "Elsewave";
        info.uniqueId = MPC_FOURCC('E', 'L', 't', 'c');
        info.version = 101;
        info.isInstrument = false;
        info.numInputs = info.numOutputs = 2;
        return info;
    }

    void addControl(mpc::Param control)
    {
        control.automatable = false;
        addParam(control);
    }

#if defined(__linux__)
    void runWorker()
    {
        const std::string helper =
            mpc::contentPath(sizeof(void*) == 4 ? "template-refresh-arm32" : "template-refresh-arm64");
        while (!stopping_)
        {
            const int action = request_.load();
            if (action < 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            notifyHostParameterChanged(kApply);
            showResult(Working);
            const int code = runHelper(helper, action);
            if (stopping_)
                break;
            showResult(code == 0 ? Updated + action : code == 3 ? Empty : code == 4 ? NoReload : Failed);
            request_ = -1;
        }
    }

    void showResult(int result)
    {
        set(kResult, result);
        notifyHostParameterChanged(kResult);
    }

    int runHelper(const std::string& helper, int action)
    {
        const char* commands[] = {"refresh", "enable", "restore"};
        char* argv[] = {const_cast<char*>(helper.c_str()), const_cast<char*>(commands[action]), nullptr};
        pid_t child;
        if (posix_spawn(&child, helper.c_str(), nullptr, nullptr, argv, environ) != 0)
            return 1;
        int status = 0;
        for (;;)
        {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child)
                return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            if (waited < 0 && errno != EINTR)
                return 1;
            if (stopping_)
                kill(child, SIGKILL);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
#endif

    std::atomic<int> request_{-1};
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

MPC_PLUGIN_ENTRY(TemplateChooser)
