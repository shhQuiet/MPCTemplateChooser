#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr char localTitle[] = "Local templates";
constexpr char hookContents[] =
    "[Service]\n"
    "# Scan drive-root template folders before launching MPC.\n"
    "ExecStartPre=-/data/local-templates/template-refresh boot\n";
constexpr char previousHookContents[] =
    "[Service]\n"
    "# Refresh indexed folders before launch, then discover new folders in the background.\n"
    "ExecStartPre=-/data/local-templates/template-refresh boot\n"
    "ExecStartPost=-/usr/bin/systemd-run --no-block --collect --unit=local-templates-discovery "
    "-p Nice=19 -p IOSchedulingClass=idle /data/local-templates/template-refresh discover\n";

struct Layout
{
    fs::path prefix;
    fs::path path(const char* absolute) const { return prefix / (absolute + 1); }
    fs::path dialog() const { return path("/sdcard/Demos/NewProjectDialog.json"); }
    fs::path state() const { return path("/data/local-templates"); }
    fs::path hook() const
    {
        return path("/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf");
    }
};

std::string readFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

void atomicWrite(const fs::path& path, const std::string& contents, mode_t mode)
{
    struct stat existing{};
    const bool present = lstat(path.c_str(), &existing) == 0;
    if (present && !S_ISREG(existing.st_mode))
        throw std::runtime_error("Expected a regular file at " + path.string());

    std::string temporary = (path.parent_path() / ".local-templates-XXXXXX").string();
    const int fd = mkstemp(temporary.data());
    if (fd < 0)
        throw std::runtime_error("Cannot stage " + path.string());

    bool ok = !present || fchown(fd, existing.st_uid, existing.st_gid) == 0;
    ok = ok && fchmod(fd, present ? existing.st_mode & 0777 : mode) == 0;
    size_t offset = 0;
    while (ok && offset < contents.size())
    {
        const ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        ok = count > 0;
        if (ok)
            offset += static_cast<size_t>(count);
    }
    ok = ok && fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    if (!ok || !closed || rename(temporary.c_str(), path.c_str()) != 0)
    {
        unlink(temporary.c_str());
        throw std::runtime_error("Cannot update " + path.string());
    }
    const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0)
    {
        fsync(directory);
        close(directory);
    }
}

class Lock
{
public:
    explicit Lock(const fs::path& path)
    {
        fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW, 0600);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open the refresh lock");
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0)
        {
            close(fd_);
            throw std::runtime_error("Another template refresh is running");
        }
    }
    ~Lock() { close(fd_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    int fd_;
};

std::string decodeMountPath(const std::string& encoded)
{
    std::string decoded;
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '\\' && i + 3 < encoded.size()
            && encoded.find_first_not_of("01234567", i + 1) >= i + 4)
        {
            decoded += static_cast<char>(std::stoi(encoded.substr(i + 1, 3), nullptr, 8));
            i += 3;
        }
        else
            decoded += encoded[i];
    }
    return decoded;
}

int drivePriority(const std::string& mount)
{
    if (mount == "/sdcard") return 0;
    if (mount == "/media/az01-internal") return 1;
    if (mount == "/data") return 2;
    if (mount.rfind("/media/", 0) == 0 || mount.rfind("/storage/", 0) == 0) return 3;
    return -1;
}

std::vector<fs::path> mountedDrives(const Layout& layout)
{
    std::istringstream mounts(readFile(layout.path("/proc/self/mountinfo")));
    std::map<std::string, std::vector<std::string>> aliases;
    std::string systemDevice;
    for (std::string line; std::getline(mounts, line);)
    {
        std::string id, parent, device, root, mount;
        std::istringstream fields(line);
        if (!(fields >> id >> parent >> device >> root >> mount))
            continue;
        mount = decodeMountPath(mount);
        if (mount == "/")
            systemDevice = device;
        if (drivePriority(mount) >= 0 && device.rfind("0:", 0) != 0)
            aliases[device + " " + root].push_back(mount);
    }
    std::vector<fs::path> drives;
    for (const auto& [device, paths] : aliases)
    {
        if (device.rfind(systemDevice + " ", 0) == 0)
            continue;
        const auto preferred = std::min_element(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
            return std::make_pair(drivePriority(a), a) < std::make_pair(drivePriority(b), b);
        });
        drives.emplace_back(*preferred);
    }
    std::sort(drives.begin(), drives.end());
    return drives;
}

struct Template
{
    std::string label;
    std::string path;
};

bool isProject(std::string_view name)
{
    if (name.size() < 5)
        return false;
    std::string suffix(name.substr(name.size() - 4));
    for (char& c : suffix)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return suffix == ".xpj";
}

void scanDirectory(const Layout& layout, const std::vector<fs::path>& drives,
                   const fs::path& directory, bool driveRoot, std::vector<Template>& found)
{
    const auto physicalPath = layout.path(directory.c_str());
    const std::unique_ptr<DIR, decltype(&closedir)> stream(opendir(physicalPath.c_str()), closedir);
    if (!stream)
    {
        if (errno == EACCES || errno == EPERM)
            return;
        throw std::runtime_error("Cannot scan " + directory.string());
    }

    for (;;)
    {
        errno = 0;
        const auto* entry = readdir(stream.get());
        if (!entry)
        {
            if (errno)
                throw std::runtime_error("Cannot finish reading " + directory.string());
            break;
        }
        const std::string_view name(entry->d_name);
        if (name.empty() || name.front() == '.')
            continue;
        if (driveRoot && name != "Templates" && name != "templates")
            continue;

        // Most MPC drives report types directly. Avoid a disk stat on every sample file.
        auto type = entry->d_type;
        if (type == DT_UNKNOWN)
        {
            struct stat info{};
            if (fstatat(dirfd(stream.get()), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0)
                continue;
            type = S_ISDIR(info.st_mode) ? DT_DIR : S_ISREG(info.st_mode) ? DT_REG : DT_UNKNOWN;
        }
        if (type == DT_DIR)
        {
            const auto child = directory / name;
            if (!std::binary_search(drives.begin(), drives.end(), child))
                scanDirectory(layout, drives, child, false, found);
        }
        else if (type == DT_REG && !driveRoot && isProject(name))
            found.push_back({std::string(name.substr(0, name.size() - 4)), (directory / name).string()});
    }
}

std::vector<Template> scan(const Layout& layout)
{
    const auto drives = mountedDrives(layout);
    std::vector<Template> found;
    for (const auto& drive : drives)
        scanDirectory(layout, drives, drive, true, found);
    std::sort(found.begin(), found.end(), [](const Template& a, const Template& b) {
        return std::tie(a.label, a.path) < std::tie(b.label, b.path);
    });
    return found;
}

Json parseDialog(const std::string& bytes)
{
    return Json::parse(bytes.substr(0, bytes.find('\0')));
}

Json& demoRow(Json& dialog)
{
    auto& rows = dialog.at("value0").at("rows");
    if (!rows.is_array())
        throw std::runtime_error("Unrecognized New Project dialog");
    Json* found = nullptr;
    for (auto& row : rows)
    {
        if (row.value("type", -1) != 1)
            continue;
        const auto title = row.value("rowTitle", "");
        if (found || (title != "DEMOS" && title != localTitle) || !row.at("items").is_array())
            throw std::runtime_error("Unrecognized Demos row");
        found = &row;
    }
    if (!found)
        throw std::runtime_error("Demos row is missing");
    return *found;
}

void writeDialog(const Layout& layout, const std::string& before, const Json& dialog)
{
    auto updated = dialog.dump(4) + "\n";
    const auto terminator = before.find('\0');
    if (terminator != std::string::npos)
        updated += before.substr(terminator);
    if (updated != before)
        atomicWrite(layout.dialog(), updated, 0644);
}

void restoreDemos(const Layout& layout)
{
    const auto before = readFile(layout.dialog());
    auto dialog = parseDialog(before);
    auto& row = demoRow(dialog);
    if (row.at("rowTitle") == "DEMOS")
        return;

    const auto original = readFile(layout.state() / "original-dialog.json");
    auto backup = parseDialog(original);
    auto& originalRow = demoRow(backup);
    if (originalRow.at("rowTitle") != "DEMOS")
        throw std::runtime_error("The original Demos row could not be restored");
    row = originalRow;
    if (dialog == backup)
        atomicWrite(layout.dialog(), original, 0644);
    else
        writeDialog(layout, before, dialog);
}

void refresh(const Layout& layout, const std::vector<Template>& templates)
{
    if (templates.empty())
    {
        restoreDemos(layout);
        return;
    }

    const auto before = readFile(layout.dialog());
    auto dialog = parseDialog(before);
    auto& row = demoRow(dialog);
    if (row.at("rowTitle") == "DEMOS")
        atomicWrite(layout.state() / "original-dialog.json", before, 0600);

    std::map<std::string, size_t> counts;
    for (const auto& item : templates)
        ++counts[item.label];
    Json items = Json::array();
    for (const auto& item : templates)
    {
        const auto parent = fs::path(item.path).parent_path();
        const auto label = counts[item.label] == 1 ? item.label : item.label + " (" + parent.string() + ")";
        items.push_back({{"icon", "project"}, {"fileName", item.path}, {"name", label}});
    }
    row["rowTitle"] = localTitle;
    row["items"] = std::move(items);
    writeDialog(layout, before, dialog);
}

bool reloadAvailable(const Layout& layout)
{
    const auto hookLink = layout.path("/usr/lib/systemd/system/acvs.service.wants/mpc-addins-reload.service");
    return fs::exists(hookLink) && fs::exists(layout.hook().parent_path() / "90-mpc-addins.conf");
}

void checkHook(const Layout& layout)
{
    if (!fs::exists(layout.hook()))
        return;
    const auto existing = readFile(layout.hook());
    if (existing != hookContents && existing != previousHookContents)
        throw std::runtime_error("Another startup configuration is using the Local templates hook path");
}

std::string revision(const Layout& layout)
{
    const auto marker = layout.state() / "revision";
    return fs::exists(marker) ? readFile(marker) : "";
}

void advanceRevision(const Layout& layout)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    atomicWrite(layout.state() / "revision", std::to_string(timestamp) + "-" + std::to_string(getpid()), 0600);
}

int main(int argc, char** argv)
{
    try
    {
        Layout layout{"/"};
#ifdef LOCAL_TEMPLATES_TEST
        if (argc == 3)
            layout.prefix = argv[2];
#else
        if (argc != 2)
            return 1;
#endif
        if (argc < 2)
            return 1;
        const std::string command = argv[1];
        if (command != "refresh" && command != "enable" && command != "restore" &&
            command != "boot" && command != "preview")
            return 1;
        alarm(command == "boot" ? 10 : 60);
        if (command == "preview")
        {
            for (const auto& item : scan(layout))
                std::cout << item.path << '\n';
            return 0;
        }
        if (!fs::is_regular_file(layout.dialog()))
            throw std::runtime_error("The MPC New Project dialog file is missing");
        fs::create_directories(layout.state());

        if (command == "restore")
        {
            const Lock lock(layout.state() / "refresh.lock");
            checkHook(layout);
            advanceRevision(layout);
            restoreDemos(layout);
            fs::remove(layout.hook());
            fs::remove(layout.state() / "template-refresh");
            return 0;
        }

        if (command == "enable")
        {
            if (!reloadAvailable(layout))
                return 4;
            const Lock lock(layout.state() / "refresh.lock");
            checkHook(layout);
            advanceRevision(layout);
            atomicWrite(layout.state() / "template-refresh", readFile(argv[0]), 0755);
            atomicWrite(layout.hook(), hookContents, 0644);
            return 0;
        }

        if (command == "boot")
        {
            // MPC X mounts external drives asynchronously, after /sdcard.
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        // Lock the live files only while publishing.
        const auto initialRevision = revision(layout);
        const auto found = scan(layout);
        const Lock lock(layout.state() / "refresh.lock");
        if (revision(layout) != initialRevision)
            throw std::runtime_error("A newer refresh finished during the scan");
        advanceRevision(layout);
        refresh(layout, found);

        std::cout << "Local templates: " << found.size() << " project(s)\n";
        return found.empty() && command == "refresh" ? 3 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Local templates: " << error.what() << '\n';
        return 1;
    }
}
