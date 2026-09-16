#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/MetaFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::serialization::TextValue;

TEST_CASE(".dvxmeta files keep identifiers, options and sub-assets", "[asset][meta]")
{
    const devex::asset::MetaFile meta{
        .id = AssetId::generate(),
        .importer = "gltf",
        .options = {{"compress_textures", TextValue(false)}},
        .subAssets = {{AssetType::Mesh, "Crate", AssetId::generate()},
                      {AssetType::Texture, "wood.png (normal)", AssetId::generate()}},
    };

    const std::string text = devex::asset::writeMetaFile(meta);
    const auto parsed = devex::asset::parseMetaFile(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->id == meta.id);
    CHECK(parsed->importer == "gltf");
    REQUIRE(parsed->options.size() == 1);
    CHECK(parsed->options[0].key == "compress_textures");
    CHECK(parsed->subAssets == meta.subAssets);

    // Sub-assets do not change the import settings; options do.
    devex::asset::MetaFile changed = *parsed;
    changed.subAssets.clear();
    CHECK(devex::asset::importSettingsHash(changed) == devex::asset::importSettingsHash(meta));
    changed.options[0].value = TextValue(true);
    CHECK(devex::asset::importSettingsHash(changed) != devex::asset::importSettingsHash(meta));
}

TEST_CASE("Invalid .dvxmeta files are reported", "[asset][meta]")
{
    CHECK_FALSE(devex::asset::parseMetaFile("[asset format=1]\n").has_value());
    CHECK_FALSE(
        devex::asset::parseMetaFile(R"([asset format=9 uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23"])")
            .has_value());
    CHECK_FALSE(devex::asset::parseMetaFile(R"([asset format=1 uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23"]
[subasset type="sound" key="x" uuid="b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"]
)")
                    .has_value());
}

TEST_CASE(".dvxmat files read every property and default the others", "[asset][material]")
{
    const auto material = devex::asset::parseMaterialFile(R"(# A comment
[material format=1]
base_color = vec4(1, 0.5, 0.25, 1)
base_color_texture = asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23")
roughness = 0.3
alpha_mode = "mask"
double_sided = true
)");
    REQUIRE(material.has_value());
    CHECK(material->baseColorFactor == devex::math::Vec4{1.0f, 0.5f, 0.25f, 1.0f});
    CHECK(material->baseColorTexture.isValid());
    CHECK(material->roughnessFactor == 0.3f);
    CHECK(material->metallicFactor == 0.0f);
    CHECK(material->alphaMode == devex::asset::AlphaMode::Mask);
    CHECK(material->doubleSided);

    const auto roundTrip =
        devex::asset::parseMaterialFile(devex::asset::writeMaterialFile(*material));
    REQUIRE(roundTrip.has_value());
    CHECK(*roundTrip == *material);

    const auto unknown = devex::asset::parseMaterialFile("[material format=1]\nshininess = 3\n");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.find("line 2") != std::string::npos);
}
