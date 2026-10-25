#include "Updater.h"

#include <fstream>
#include <sstream>

#include "Http.h"
#include "Json.h"
#include "Platform.h"
#include "SemVer.h"
#include "Sha256.h"
#include "Signature.h"
#include "Zip.h"

namespace {

const char kGitHubJson[] = "application/vnd.github+json";

ReleaseInfo ReadRelease(const Json &json) {
    ReleaseInfo release;
    release.tag = json["tag_name"].AsString();
    SemVer version;
    if (SemVer::Parse(release.tag, &version))
        release.version = version.ToString();
    release.title = json["name"].AsString(release.tag);
    release.notes = json["body"].AsString();
    release.page = json["html_url"].AsString();
    release.prerelease = json["prerelease"].AsBool();
    for (const Json &asset : json["assets"].Items())
        release.assets.push_back({asset["name"].AsString(), asset["browser_download_url"].AsString()});
    return release;
}

bool ReadFile(const fs::path &file, std::string *text) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    *text = buffer.str();
    return true;
}

bool Fail(InstallResult *result, const std::string &error) {
    result->ok = false;
    result->error = error;
    return false;
}

// What the zip's (or the folder's) unpacked files become: app/<version>/, the launcher replaced where it is older,
// and launcher.json switched to it. With minLauncher above this launcher, the launcher alone.
bool Finish(Installation &installation, const std::string &version, const fs::path &unpacked,
            const std::string &launcherVersion, const std::string &minLauncher, bool afterRestart,
            InstallResult *result) {
    std::error_code ec;
    if (!fs::is_regular_file(unpacked / (std::string("action_standalone") + Platform_ExecutableSuffix()), ec)) {
        fs::remove_all(unpacked, ec);
        return Fail(result, "the release has no action_standalone for this platform");
    }
    fs::path releaseLauncher = unpacked / installation.LauncherFile().filename();
    bool haveLauncher = fs::is_regular_file(releaseLauncher, ec);

    result->launcherRequired = !minLauncher.empty() && SemVer_Compare(minLauncher, launcherVersion) > 0;
    if (result->launcherRequired) {
        std::string error;
        if (afterRestart)
            error = "the release needs launcher " + minLauncher + ", and its own is " + launcherVersion;
        else if (SemVer_Compare(minLauncher, version) > 0)
            error = "the release needs launcher " + minLauncher + ", newer than the release itself";
        else if (!haveLauncher)
            error = "the release needs launcher " + minLauncher + " or later, and carries none";
        else if (Updater_ReplaceLauncher(releaseLauncher, installation.LauncherFile(), &error))
            result->restartToFinish = true;
        else
            error = "the release needs a newer launcher, which could not be put in place: " + error;
        fs::remove_all(unpacked, ec);
        if (!result->restartToFinish)
            return Fail(result, error);
        result->ok = true;
        result->launcherReplaced = true;
        result->version = version;
        return true;
    }

    fs::path folder = installation.VersionFolder(version);
    fs::remove_all(folder, ec);
    fs::rename(unpacked, folder, ec);
    if (ec) {
        fs::remove_all(unpacked, ec);
        return Fail(result, "cannot move the release into " + ToUtf8(folder) + ": " + ec.message());
    }

    // The release's launcher, where it is newer than this one or there is none in the root yet
    fs::path newLauncher = folder / installation.LauncherFile().filename();
    if (haveLauncher &&
        (SemVer_Compare(version, launcherVersion) > 0 || !fs::exists(installation.LauncherFile(), ec))) {
        std::string error;
        result->launcherReplaced = Updater_ReplaceLauncher(newLauncher, installation.LauncherFile(), &error);
    }

    LauncherConfig &config = installation.Config();
    if (config.installed != version) {
        config.previous = config.installed;
        config.installed = version;
    }
    std::string error;
    if (!installation.Save(&error))
        return Fail(result, error);
    installation.Prune();
    result->ok = true;
    result->version = version;
    return true;
}

} // namespace

const ReleaseAsset *ReleaseInfo::Asset(const std::string &name) const {
    for (const ReleaseAsset &asset : assets)
        if (asset.name == name)
            return &asset;
    return nullptr;
}

UpdateCheck Updater_Check(const Installation &installation) {
    UpdateCheck check;
    const LauncherConfig &config = installation.Config();
    std::string base = config.apiBase + "/repos/" + config.repository + "/releases";
    std::string body;
    bool stable = config.channel == Channel::Stable;
    HttpResult http = Http_Get(stable ? base + "/latest" : base + "?per_page=30", &body, kGitHubJson);
    if (!http.Ok()) {
        check.offline = http.failure == HttpFailure::Network;
        check.error = http.status == 404 && stable ? "no release has been published yet" : http.error;
        return check;
    }
    Json json;
    std::string why;
    if (!Json::Parse(body, &json, &why)) {
        check.error = "the releases list could not be read (" + why + ")";
        return check;
    }
    if (stable) {
        check.release = ReadRelease(json);
    } else {
        // The newest by version, pre-releases included, drafts not
        bool found = false;
        for (const Json &item : json.Items()) {
            if (item["draft"].AsBool())
                continue;
            ReleaseInfo release = ReadRelease(item);
            if (release.version.empty())
                continue;
            if (!found || SemVer_Compare(release.version, check.release.version) > 0) {
                check.release = release;
                found = true;
            }
        }
        if (!found) {
            check.error = "no release has been published yet";
            return check;
        }
    }
    if (check.release.version.empty()) {
        check.error = "the newest release's tag (" + check.release.tag + ") is not a version";
        return check;
    }
    check.ok = true;
    check.newer = config.installed.empty() || SemVer_Compare(check.release.version, config.installed) > 0;
    return check;
}

InstallResult Updater_Install(Installation &installation, const ReleaseInfo &release,
                              const std::string &launcherVersion, const UpdateProgress &progress, bool afterRestart) {
    InstallResult result;
    auto report = [&](const std::string &step, double fraction) {
        if (progress.report)
            progress.report(step, fraction);
    };
    auto cancelled = [&]() { return progress.cancel != nullptr && progress.cancel->load(); };

    // 1. The manifest and its signature
    const ReleaseAsset *manifestAsset = release.Asset("manifest.json");
    const ReleaseAsset *signatureAsset = release.Asset("manifest.json.sig");
    if (manifestAsset == nullptr || signatureAsset == nullptr) {
        Fail(&result, "release " + release.version + " has no signed manifest");
        return result;
    }
    report("Checking the release's signature", 0);
    std::string manifestText, signatureText;
    HttpResult http = Http_Get(manifestAsset->url, &manifestText);
    if (http.Ok())
        http = Http_Get(signatureAsset->url, &signatureText);
    if (!http.Ok()) {
        Fail(&result, http.error);
        return result;
    }
    std::string error;
    if (!Signature_Verify(manifestText, signatureText, &error)) {
        Fail(&result, error);
        return result;
    }
    Json manifest;
    std::string why;
    if (!Json::Parse(manifestText, &manifest, &why) || !manifest.IsObject()) {
        Fail(&result, "the manifest could not be read (" + why + ")");
        return result;
    }
    std::string version = manifest["version"].AsString();
    if (version != release.version) {
        Fail(&result, "the manifest is for version " + version + ", not the release's " + release.version);
        return result;
    }
    const Json &platform = manifest["platforms"][Platform_Name()];
    std::string zipName = platform["zip"].AsString();
    uint64_t zipSize = uint64_t(platform["size"].AsNumber(-1));
    std::string zipHash = platform["sha256"].AsString();
    if (zipName.empty() || zipHash.size() != 64) {
        Fail(&result, std::string("release ") + version + " has no build for " + Platform_Name() + " yet");
        return result;
    }
    const ReleaseAsset *zipAsset = release.Asset(zipName);
    if (zipAsset == nullptr) {
        Fail(&result, "the release lacks " + zipName);
        return result;
    }

    // 2. The zip, its size and hash checked
    std::error_code ec;
    fs::path downloads = installation.AppFolder() / ".download";
    fs::create_directories(downloads, ec);
    fs::path zipFile = downloads / FromUtf8(zipName);
    http = Http_Download(zipAsset->url, zipFile, [&](uint64_t received, uint64_t total) {
        if (total == 0)
            total = zipSize;
        report("Downloading " + zipName, total > 0 ? double(received) / double(total) : 0);
        return !cancelled();
    });
    if (!http.Ok()) {
        fs::remove(zipFile, ec);
        Fail(&result, http.error);
        return result;
    }
    report("Checking the download", 1);
    uint64_t size = fs::file_size(zipFile, ec);
    std::string hash;
    {
        std::ifstream in(zipFile, std::ios::binary);
        Sha256 sha;
        std::vector<char> buffer(1 << 20);
        while (in) {
            in.read(buffer.data(), std::streamsize(buffer.size()));
            if (in.gcount() > 0)
                sha.Update(buffer.data(), size_t(in.gcount()));
        }
        hash = sha.FinalHex();
    }
    if (size != zipSize || hash != zipHash) {
        fs::remove(zipFile, ec);
        Fail(&result, "the download is not the release's file (" + std::string(size != zipSize ? "size" : "SHA-256") +
                          " differs from the manifest)");
        return result;
    }

    // 3. Unpacked beside, then moved into place
    fs::path unpacked = installation.AppFolder() / FromUtf8(version + ".partial");
    fs::remove_all(unpacked, ec);
    fs::create_directories(unpacked, ec);
    if (!Zip_Extract(zipFile, unpacked, &error, [&](uint64_t done, uint64_t total) {
            report("Unpacking", total > 0 ? double(done) / double(total) : 0);
        })) {
        fs::remove_all(unpacked, ec);
        fs::remove(zipFile, ec);
        Fail(&result, error);
        return result;
    }
    fs::remove(zipFile, ec);

    // 4. The launcher, and the switch; the repository the manifest names is kept for the next check
    if (!Finish(installation, version, unpacked, launcherVersion, manifest["minLauncher"].AsString(), afterRestart,
                &result))
        return result;
    std::string repository = manifest["repository"].AsString();
    if (!repository.empty() && repository != installation.Config().repository) {
        installation.Config().repository = repository;
        installation.Save(&error);
    }
    return result;
}

bool Updater_InstallFolder(Installation &installation, const fs::path &folder, const std::string &launcherVersion,
                           InstallResult *result) {
    std::string text;
    if (!ReadFile(folder / "nightfire-release.json", &text))
        return false;
    Json json;
    std::string why;
    if (!Json::Parse(text, &json, &why) || json["platform"].AsString() != Platform_Name())
        return Fail(result, "the release beside the launcher is not for " + std::string(Platform_Name()));
    std::string version = json["version"].AsString();
    SemVer parsed;
    if (!SemVer::Parse(version, &parsed))
        return Fail(result, "the release beside the launcher has no version");

    std::error_code ec;
    fs::path unpacked = installation.AppFolder() / FromUtf8(version + ".partial");
    fs::remove_all(unpacked, ec);
    fs::create_directories(unpacked, ec);
    for (const Json &file : json["files"].Items()) {
        fs::path relative = FromUtf8(file.AsString());
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
            ToUtf8(relative).find("..") != std::string::npos) {
            fs::remove_all(unpacked, ec);
            return Fail(result, "nightfire-release.json names a file outside its folder");
        }
        fs::create_directories((unpacked / relative).parent_path(), ec);
        fs::copy_file(folder / relative, unpacked / relative, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove_all(unpacked, ec);
            return Fail(result, "cannot copy " + ToUtf8(relative) + ": " + ec.message());
        }
    }
    // The launcher running this is the folder's own, so minLauncher does not apply
    return Finish(installation, version, unpacked, launcherVersion, "", false, result);
}

bool Updater_Undo(Installation &installation, std::string *error) {
    LauncherConfig &config = installation.Config();
    std::error_code ec;
    if (config.previous.empty() || !fs::is_regular_file(installation.GameExecutable(config.previous), ec)) {
        *error = "there is no previous version to go back to";
        return false;
    }
    std::swap(config.installed, config.previous);
    return installation.Save(error);
}

bool Updater_ReplaceLauncher(const fs::path &newLauncher, const fs::path &target, std::string *error) {
    std::error_code ec;
    fs::path staged = target.parent_path() / ("launcher.new" + std::string(Platform_ExecutableSuffix()));
    fs::path old = target.parent_path() / ("launcher.old" + std::string(Platform_ExecutableSuffix()));
    fs::copy_file(newLauncher, staged, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        *error = "cannot write " + ToUtf8(staged) + ": " + ec.message();
        return false;
    }
    if (fs::exists(target, ec)) {
        fs::remove(old, ec);
        fs::rename(target, old, ec);
        if (ec) {
            *error = "cannot move the old launcher aside: " + ec.message();
            fs::remove(staged, ec);
            return false;
        }
    }
    fs::rename(staged, target, ec);
    if (ec) {
        *error = "cannot put the new launcher in place: " + ec.message();
        std::error_code restoreError;
        fs::rename(old, target, restoreError);
        return false;
    }
    return true;
}

void Updater_CleanUp(const Installation &installation) {
    std::error_code ec;
    fs::remove(installation.Root() / ("launcher.old" + std::string(Platform_ExecutableSuffix())), ec);
    fs::remove(installation.Root() / ("launcher.new" + std::string(Platform_ExecutableSuffix())), ec);
}
