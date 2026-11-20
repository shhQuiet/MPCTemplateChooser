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
        auto reserved = mpc::boolParam("reserved", "Reserved", false);
        auto enable = mpc::boolParam("enable", "Enable templates", false);
        reserved.automatable = enable.automatable = false;
        addParam(reserved);
        addParam(enable);
#if defined(__linux__)
        setFromDisk();
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
        if (index == kEnable && !reflectingState_)
            request_ = value >= 0.5f;
#else
        (void)index;
        (void)value;
#endif
    }

#if defined(__linux__)
    static VstIntPtr dispatch(AEffect* effect, VstInt32 opcode, VstInt32 index,
                             VstIntPtr value, void* data, float option)
    {
        if (opcode != effSetChunk)
            return mpc::detail::dispatcher(effect, opcode, index, value, data, option);

        // Opening a project must never install or remove a device-wide hook.
        auto* chooser = static_cast<TemplateChooser*>(effect->object);
        reflectingState_ = true;
        const auto result = mpc::detail::dispatcher(effect, opcode, index, value, data, option);
        chooser->set(kEnable, startupEnabled());
        reflectingState_ = false;
        return result;
    }
#endif

private:
    enum { kEnable = 1 }; // Parameter 0 absorbs MPC's startup nudge.

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "MPC Template Chooser";
        info.vendor = "Elsewave";
        info.uniqueId = MPC_FOURCC('E', 'L', 't', 'c');
        info.version = 104;
        info.isInstrument = false;
        info.numInputs = info.numOutputs = 2;
        return info;
    }

#if defined(__linux__)
    static bool startupEnabled()
    {
        return access("/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf", F_OK) == 0;
    }

    void setFromDisk()
    {
        reflectingState_ = true;
        set(kEnable, startupEnabled());
        notifyHostParameterChanged(kEnable);
        reflectingState_ = false;
    }

    void runWorker()
    {
        const std::string helper =
            mpc::contentPath(sizeof(void*) == 4 ? "template-refresh-arm32" : "template-refresh-arm64");
        while (!stopping_)
        {
            const int action = request_.exchange(-1);
            if (action < 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if ((action != 0) != startupEnabled() && runHelper(helper, action ? "enable" : "restore") != 0)
                mpc::log("MPC Template Chooser: could not change startup setting");
            if (!stopping_ && request_.load() < 0)
                setFromDisk();
        }
    }

    struct TemporaryHelper
    {
        char path[sizeof("/tmp/mpc-template-chooser-XXXXXX")] = "/tmp/mpc-template-chooser-XXXXXX";
        bool created = false;

        ~TemporaryHelper()
        {
            if (created)
                unlink(path);
        }

        bool copyFrom(const std::string& source)
        {
            std::vector<uint8_t> bytes;
            if (!mpc::readFile(source, bytes))
                return false;

            const int fd = mkstemp(path);
            if (fd < 0)
                return false;
            created = true;
            size_t written = 0;
            while (written < bytes.size())
            {
                const auto count = write(fd, bytes.data() + written, bytes.size() - written);
                if (count < 0 && errno == EINTR)
                    continue;
                if (count <= 0)
                    break;
                written += static_cast<size_t>(count);
            }
            const bool ready = written == bytes.size() && fchmod(fd, 0700) == 0;
            return close(fd) == 0 && ready;
        }
    };

    int runHelper(const std::string& helper, const char* action)
    {
        pid_t child;
        const auto spawn = [&](const char* path) {
            char* args[] = {const_cast<char*>(path), const_cast<char*>(action), nullptr};
            return posix_spawn(&child, path, nullptr, nullptr, args, environ);
        };
        TemporaryHelper temporary;
        int error = spawn(helper.c_str());
        if (error == EACCES && temporary.copyFrom(helper))
            error = spawn(temporary.path);
        if (error != 0)
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

    inline static thread_local bool reflectingState_ = false;
    std::atomic<int> request_{-1};
#endif
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

#if defined(__linux__)
extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0)
        return nullptr;
    auto* effect = mpc::detail::createEffect(new TemplateChooser(), host);
    if (effect)
        effect->dispatcher = &TemplateChooser::dispatch;
    return effect;
}
#else
MPC_PLUGIN_ENTRY(TemplateChooser)
#endif
