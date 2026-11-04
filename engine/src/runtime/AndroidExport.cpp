// Exporting a game for Android: the package of the game, its C++ module built with the NDK for every
// ABI, and the Gradle project that the engine builds for Android leave in their folders, filled with
// them and turned into an APK or an AAB. The Gradle project lives in the cache of the project, whose
// caches make the next export quick.
#include <devex/runtime/GameExport.hpp>

#include "GameCodeBuilder.hpp"

#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>
#include <utility>

namespace devex::runtime {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr std::string_view programSuffix = ".exe";
#else
constexpr std::string_view programSuffix = "";
#endif

[[nodiscard]] fs::path environmentPath(const char* name)
{
    return core::pathFromUtf8(platform::environmentVariable(name));
}

[[nodiscard]] bool isDirectory(const fs::path& path)
{
    std::error_code error;
    return !path.empty() && fs::is_directory(path, error);
}

[[nodiscard]] fs::path findSdk()
{
    const fs::path home = environmentPath(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    for (const fs::path& candidate : {environmentPath("ANDROID_HOME"), environmentPath("ANDROID_SDK_ROOT"),
#ifdef _WIN32
                                      environmentPath("LOCALAPPDATA") / "Android" / "Sdk",
#else
                                      home / "Android" / "Sdk", home / "Library" / "Android" / "sdk",
#endif
         })
    {
        if (isDirectory(candidate / "platform-tools") || isDirectory(candidate / "platforms"))
        {
            return candidate;
        }
    }
    return {};
}

// The NDK named by the environment, or the newest of the SDK.
[[nodiscard]] fs::path findNdk(const fs::path& sdk)
{
    for (const fs::path& candidate : {environmentPath("ANDROID_NDK_HOME"), environmentPath("ANDROID_NDK_ROOT")})
    {
        if (isDirectory(candidate / "build" / "cmake"))
        {
            return candidate;
        }
    }
    std::vector<fs::path> versions;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(sdk / "ndk", error))
    {
        if (isDirectory(entry.path() / "build" / "cmake"))
        {
            versions.push_back(entry.path());
        }
    }
    std::ranges::sort(versions);
    return versions.empty() ? fs::path{} : versions.back();
}

// JAVA_HOME, the JDK that comes with Android Studio, or the one the system finds.
[[nodiscard]] fs::path findJava()
{
    const std::string java = std::format("java{}", programSuffix);
    std::vector<fs::path> homes{environmentPath("JAVA_HOME")};
#ifdef _WIN32
    homes.push_back(environmentPath("ProgramFiles") / "Android" / "Android Studio" / "jbr");
#else
    homes.push_back(environmentPath("HOME") / "android-studio" / "jbr");
    homes.emplace_back("/opt/android-studio/jbr");
    homes.emplace_back("/Applications/Android Studio.app/Contents/jbr/Contents/Home");
#endif
    for (const fs::path& home : homes)
    {
        std::error_code error;
        if (!home.empty() && fs::is_regular_file(home / "bin" / java, error))
        {
            return home / "bin" / java;
        }
    }
    return java;
}

// A value of a properties file, where a backslash starts an escape.
[[nodiscard]] std::string propertyValue(std::string_view value)
{
    std::string escaped;
    for (const char character : value)
    {
        if (character == '\\')
        {
            escaped += '\\';
        }
        if (character != '\n' && character != '\r')
        {
            escaped += character;
        }
    }
    return escaped;
}

// Copies a folder over another, replacing the files they share.
[[nodiscard]] core::Result<void> copyOver(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    fs::create_directories(destination, error);
    if (!error)
    {
        fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    }
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "Cannot copy {}: {}", core::toUtf8(source), error.message());
    }
    return {};
}

} // namespace

AndroidTools findAndroidTools()
{
    AndroidTools tools;
    tools.sdk = findSdk();
    if (!tools.sdk.empty())
    {
        tools.ndk = findNdk(tools.sdk);
        tools.adb = tools.sdk / "platform-tools" / std::format("adb{}", programSuffix);
    }
    tools.java = findJava();
    return tools;
}

std::string AndroidTools::problem(bool needsNdk) const
{
    if (sdk.empty())
    {
        return "The Android SDK is not found: install Android Studio, or set ANDROID_HOME";
    }
    if (needsNdk && ndk.empty())
    {
        return "The Android NDK is not found: install it from the SDK Manager of Android Studio, or set ANDROID_NDK_HOME";
    }
    return {};
}

std::vector<AndroidBuild> findAndroidBuilds(const fs::path& binDirectory)
{
    std::vector<AndroidBuild> builds;
    fs::path bin = binDirectory.lexically_normal();
    if (!bin.has_filename())
    {
        bin = bin.parent_path();
    }
    std::vector<fs::path> folders;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(bin.parent_path().parent_path(), error))
    {
        if (entry.is_directory(error))
        {
            folders.push_back(entry.path());
        }
    }
    std::ranges::sort(folders);
    for (const fs::path& folder : folders)
    {
        for (const fs::directory_entry& abi : fs::directory_iterator(folder / "android" / "app" / "src" / "main" / "jniLibs", error))
        {
            if (fs::is_regular_file(abi.path() / "libdevex-engine.so", error) || fs::is_regular_file(abi.path() / "libc++_shared.so", error))
            {
                builds.push_back({.abi = core::toUtf8(abi.path().filename()),
                                  .engine = {.name = core::toUtf8(folder.filename()), .configuration = "Release", .directory = folder}});
            }
        }
        error.clear();
    }
    return builds;
}

std::string androidGradleProperties(const ExportPlan& plan, bool hasIcon)
{
    const asset::AndroidExportSettings& android = plan.project.exportSettings.android;
    const std::string packageName = android.package.empty() ? asset::androidPackageFromName(plan.project.name) : android.package;
    std::string abis;
    for (const AndroidBuild& build : plan.androidBuilds)
    {
        abis += (abis.empty() ? "" : ",") + build.abi;
    }
    const std::string_view orientation = android.orientation == "portrait" ? "sensorPortrait"
                                         : android.orientation == "sensor" ? "fullSensor"
                                                                           : "sensorLandscape";
    std::string text = std::format("applicationId={}\nversionName={}\nversionCode={}\ngameName={}\norientation={}\nabis={}\nicon={}\n",
                                   propertyValue(packageName), propertyValue(android.versionName), android.versionCode,
                                   propertyValue(plan.project.name), orientation, abis,
                                   hasIcon ? "@mipmap/ic_launcher" : "@android:drawable/sym_def_app_icon");
    if (android.release)
    {
        text += std::format("keystore={}\nkeyAlias={}\nstorePassword={}\nkeyPassword={}\n", propertyValue(core::toUtf8(plan.signing.keystore)),
                            propertyValue(plan.signing.alias), propertyValue(plan.signing.storePassword),
                            propertyValue(plan.signing.keyPassword));
    }
    return text;
}

core::Result<void> prepareAndroidExport(ExportPlan& plan, const fs::path& binDirectory)
{
    const asset::AndroidExportSettings& android = plan.project.exportSettings.android;
    plan.platform = ExportPlatform::Android;
    plan.managed = false;
    std::vector<AndroidBuild> builds = findAndroidBuilds(binDirectory);
    std::erase_if(builds, [&](const AndroidBuild& build) {
        return !(build.abi == "arm64-v8a" ? android.arm64 : build.abi == "x86_64" ? android.x86_64 : false);
    });
    if (builds.empty())
    {
        return core::makeError(core::ErrorCode::NotFound,
                               "There is no Android build of the engine for the chosen architectures next to the editor: build the "
                               "android-arm64 or android-x86_64 preset first");
    }
    plan.androidBuilds = std::move(builds);
    plan.engine = plan.androidBuilds.front().engine;
    const fs::path output = core::pathFromUtf8(android.output);
    plan.output = (output.is_absolute() ? output : plan.project.root / output).lexically_normal();
    plan.signing = asset::readAndroidSigning(plan.project);
    if (android.release && (plan.signing.keystore.empty() || plan.signing.alias.empty()))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "A release export needs a key: fill in its keystore and alias");
    }
    if (const std::string packageName = android.package.empty() ? asset::androidPackageFromName(plan.project.name) : android.package;
        !asset::validAndroidPackage(packageName))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "{} is not a valid Android package name", packageName);
    }
    return {};
}

core::Result<ExportResult> exportAndroid(const ExportPlan& plan, const std::function<void(const ExportProgress&)>& progress,
                                         const std::atomic<bool>* cancel)
{
    const auto start = std::chrono::steady_clock::now();
    const auto report = [&progress](std::string step, float fraction) {
        if (progress)
        {
            progress({.step = std::move(step), .fraction = fraction});
        }
    };
    const asset::Project& project = plan.project;
    const asset::AndroidExportSettings& android = project.exportSettings.android;
    const std::string name = executableName(project.name);
    const bool hasNative = detail::GameCodeBuilder::hasCode(project);
    const AndroidTools tools = findAndroidTools();
    if (const std::string problem = tools.problem(hasNative); !problem.empty())
    {
        return core::makeError(core::ErrorCode::NotFound, "{}", problem);
    }
    if (plan.androidBuilds.empty())
    {
        return core::makeError(core::ErrorCode::NotFound, "There is no Android build of the engine to export with");
    }

    // The package, with its textures in ASTC for phones. An emulator on a PC reads BC, and decodes ASTC
    // wrongly: with nothing but x86_64, they stay as they are.
    const fs::path work = project.cacheDirectory() / "android";
    ExportPlan packagePlan = plan;
    packagePlan.packageOnly = true;
    packagePlan.output = work / "package";
    packagePlan.androidConvertsTextures = std::ranges::any_of(plan.androidBuilds, [](const AndroidBuild& build) { return build.abi != "x86_64"; });
    core::Result<ExportResult> packed = exportGame(
        packagePlan,
        [&](const ExportProgress& step) { report(step.step, step.fraction * 0.45f); },
        cancel);
    if (!packed)
    {
        return std::unexpected(packed.error());
    }

    // The Gradle project, without what the previous export filled it with, keeping its caches.
    report("Preparing the Android project", 0.47f);
    const fs::path gradle = work / "gradle";
    const fs::path main = gradle / "app" / "src" / "main";
    std::error_code error;
    fs::remove_all(gradle / "app" / "src", error);
    if (core::Result<void> copied = copyOver(plan.androidBuilds.front().engine.directory / "android", gradle); !copied)
    {
        return std::unexpected(copied.error());
    }
    fs::remove_all(main / "jniLibs", error);
    float fraction = 0.5f;
    for (const AndroidBuild& build : plan.androidBuilds)
    {
        if (core::Result<void> copied = copyOver(build.engine.directory / "android" / "app" / "src" / "main" / "jniLibs" / build.abi, main / "jniLibs" / build.abi);
            !copied)
        {
            return std::unexpected(copied.error());
        }
        if (hasNative)
        {
            report(std::format("Building the game code for {}", build.abi), fraction);
            core::Result<fs::path> library = detail::GameCodeBuilder::buildForAndroid(project, build.engine.cmakeDirectory(), tools.ndk, build.abi, cancel);
            if (!library)
            {
                return std::unexpected(library.error());
            }
            fs::copy_file(*library, main / "jniLibs" / build.abi / library->filename(), fs::copy_options::overwrite_existing, error);
            if (error)
            {
                return core::makeError(core::ErrorCode::Io, "Cannot copy the game code: {}", error.message());
            }
        }
        fraction += 0.2f / static_cast<float>(plan.androidBuilds.size());
    }

    report("Putting the game in the Android project", 0.7f);
    fs::create_directories(main / "assets", error);
    fs::copy_file(packagePlan.output / (name + ".dvxpak"), main / "assets" / "game.dvxpak", fs::copy_options::overwrite_existing, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "Cannot copy the package: {}", error.message());
    }
    bool hasIcon = false;
    if (plan.icon)
    {
        const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(*plan.icon);
        const core::Result<asset::Image> image = bytes ? asset::decodeImage(*bytes) : std::unexpected(bytes.error());
        if (image)
        {
            const asset::Image resized = asset::resizeImage(*image, 192, 192);
            fs::create_directories(main / "res" / "mipmap-xxxhdpi", error);
            hasIcon = static_cast<bool>(core::writeFileAtomically(main / "res" / "mipmap-xxxhdpi" / "ic_launcher.png", asset::encodePng(resized)));
        }
        if (!hasIcon)
        {
            DEVEX_LOG_WARNING("The game keeps the default icon: its image could not be read");
        }
    }
    if (core::Result<void> written = core::writeTextFile(gradle / "devex.properties", androidGradleProperties(plan, hasIcon)); !written)
    {
        return std::unexpected(written.error());
    }
    if (core::Result<void> written = core::writeTextFile(gradle / "local.properties", std::format("sdk.dir={}\n", propertyValue(core::toUtf8(tools.sdk)))); !written)
    {
        return std::unexpected(written.error());
    }

    // Started with the java found rather than through gradlew, which would look for it by itself.
    const bool bundle = android.format == "aab";
    const std::string_view variant = android.release ? "Release" : "Debug";
    const std::string task = std::format("{}{}", bundle ? "bundle" : "assemble", variant);
    report(std::format("Building the {}: the first time, Gradle downloads what it needs", bundle ? "AAB" : "APK"), 0.72f);
    if (core::Result<void> built = platform::runAndLog({core::toUtf8(tools.java), "-classpath", core::toUtf8(gradle / "gradle" / "wrapper" / "gradle-wrapper.jar"),
                                              "org.gradle.wrapper.GradleWrapperMain", "--no-daemon", "--console=plain", task},
                                             gradle, "Gradle could not build the game", cancel);
        !built)
    {
        return std::unexpected(built.error());
    }
    const std::string lower = android.release ? "release" : "debug";
    const fs::path made = bundle ? gradle / "app" / "build" / "outputs" / "bundle" / lower / std::format("app-{}.aab", lower)
                                 : gradle / "app" / "build" / "outputs" / "apk" / lower / std::format("app-{}.apk", lower);
    fs::create_directories(plan.output, error);
    const fs::path destination = plan.output / std::format("{}.{}", name, bundle ? "aab" : "apk");
    fs::copy_file(made, destination, fs::copy_options::overwrite_existing, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "Cannot copy {}: {}", core::toUtf8(made), error.message());
    }

    if (android.install)
    {
        if (bundle)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "An AAB does not install on a device: it goes to the Play Store. Export an APK to try the game");
        }
        report("Installing on the device", 0.95f);
        if (core::Result<void> installed = platform::runAndLog({core::toUtf8(tools.adb), "install", "-r", core::toUtf8(destination)}, gradle,
                                                     "Cannot install the game: is a device connected, with USB debugging on?", cancel);
            !installed)
        {
            return std::unexpected(installed.error());
        }
        const std::string packageName = android.package.empty() ? asset::androidPackageFromName(project.name) : android.package;
        if (core::Result<void> started = platform::runAndLog({core::toUtf8(tools.adb), "shell", "am", "start", "-n", std::format("{}/com.devex.player.DevexActivity", packageName)},
                                                   gradle, "Cannot start the game on the device", cancel);
            !started)
        {
            return std::unexpected(started.error());
        }
    }
    report("Exported", 1.0f);
    return ExportResult{.output = plan.output,
                        .executable = destination,
                        .assets = packed->assets,
                        .packageBytes = packed->packageBytes,
                        .seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()};
}

} // namespace devex::runtime
