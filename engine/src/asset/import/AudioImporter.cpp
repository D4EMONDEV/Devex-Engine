#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/audio/Clip.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#include <utility>

namespace devex::asset {
namespace {

// Clips up to this long are decoded once when "loading" is "auto"; longer ones stream.
constexpr double decodedSecondsLimit = 10.0;

} // namespace

core::Result<ImportResult> importAudioFile(ImportContext& context)
{
    core::Result<std::vector<std::byte>> file = core::readBinaryFile(context.source);
    if (!file)
    {
        return std::unexpected(file.error());
    }
    const core::Result<audio::ClipInfo> info = audio::probeClip(*file);
    if (!info)
    {
        return core::makeError(info.error().code, "{}: {}", core::toUtf8(context.source.filename()), info.error().message);
    }

    AudioClipData clip{
        .encoding = info->encoding,
        .channels = info->channels,
        .sampleRate = info->sampleRate,
        .frames = info->frames,
        .waveform = info->waveform,
        .encoded = std::move(*file),
    };
    const std::string loading = context.stringOption("loading", "auto");
    if (loading == "decoded")
    {
        clip.loading = AudioLoading::Decoded;
    }
    else if (loading == "streamed")
    {
        clip.loading = AudioLoading::Streamed;
    }
    else
    {
        clip.loading = clip.seconds() <= decodedSecondsLimit ? AudioLoading::Decoded : AudioLoading::Streamed;
    }

    ImportResult result;
    result.artifacts.push_back({context.mainId, AssetType::AudioClip, context.name, encodeAudioClip(clip)});
    return result;
}

} // namespace devex::asset
