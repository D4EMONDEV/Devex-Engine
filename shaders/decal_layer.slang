// What a decal lays on a surface before it is lit, shared by Vulkan (geometry.slang) and WebGPU (webgpu_mesh.slang),
// generic over where its textures are read: it covers by its albedo's alpha, faded towards the top and the bottom of
// its box, on surfaces turning away from it and far from the camera, as Godot's decals.
module decal_layer;

// What the decals change of a surface before it is lit.
public struct DecalLayer
{
    public float3 baseColor;
    public float3 normal;
    public float metallic;
    public float roughness;
    public float occlusion;
    public float3 emission;
};

// The box of a decal and its fades, as both renderers pack them.
public struct DecalShape
{
    // From the world into its box: X and Z from -1 to 1 across it, Y from -1 at its bottom to 1 at its top, the side it
    // projects from.
    public float4x4 worldToDecal;
    // Its axes in the world, normalized: its normal map tilts along X and Z, and faces Y.
    public float4 axisX;
    public float4 axisZ;
    public float4 axisY;
    public float4 modulate;
    public float albedoMix;
    public float emissionEnergy;
    public float normalFade;
    public float upperFade;
    public float lowerFade;
    public float fadeBegin;
    public float fadeLength;
};

// The textures of a decal: which it has, and each read across its box with the slopes given.
public static const uint decalAlbedo = 0;
public static const uint decalNormal = 1;
public static const uint decalOrm = 2;
public static const uint decalEmission = 3;

public interface IDecalTextures
{
    bool has(uint kind);
    float4 read(uint kind, float2 uv, float2 slopeX, float2 slopeY);
};

// Lays one decal on the layer, where the position falls inside its box. The slopes of the position are taken before
// any loop, where neighbouring pixels may part.
public void layDecal<T : IDecalTextures>(DecalShape decal, T textures, float3 position, float3 facing, float viewDistance,
                                         float3 positionDx, float3 positionDy, inout DecalLayer layer)
{
    const float3 local = mul(decal.worldToDecal, float4(position, 1.0)).xyz;
    if (any(abs(local) > 1.0))
    {
        return;
    }
    float fade = decal.modulate.a * pow(1.0 - abs(local.y), local.y > 0.0 ? decal.upperFade : decal.lowerFade);
    if (decal.normalFade > 0.0)
    {
        fade *= smoothstep(decal.normalFade, 1.0, dot(facing, decal.axisY.xyz) * 0.5 + 0.5);
    }
    if (decal.fadeLength > 0.0)
    {
        fade *= saturate((decal.fadeBegin + decal.fadeLength - viewDistance) / decal.fadeLength);
    }
    if (fade <= 0.0)
    {
        return;
    }

    // Its textures across X and Z, with slopes taken in its box, so that its edges do not pick the smallest mips.
    const float2 uv = local.xz * 0.5 + 0.5;
    const float2 uvDx = mul((float3x3)decal.worldToDecal, positionDx).xz * 0.5;
    const float2 uvDy = mul((float3x3)decal.worldToDecal, positionDy).xz * 0.5;
    float coverage = fade;
    if (textures.has(decalAlbedo))
    {
        const float4 albedo = textures.read(decalAlbedo, uv, uvDx, uvDy);
        coverage = albedo.a * fade;
        layer.baseColor = lerp(layer.baseColor, albedo.rgb * decal.modulate.rgb, coverage * decal.albedoMix);
    }
    if (textures.has(decalNormal))
    {
        const float2 tilt = textures.read(decalNormal, uv, uvDx, uvDy).rg * 2.0 - 1.0;
        const float3 decalNormal =
            decal.axisX.xyz * tilt.x + decal.axisZ.xyz * tilt.y + decal.axisY.xyz * sqrt(saturate(1.0 - dot(tilt, tilt)));
        layer.normal = normalize(lerp(layer.normal, decalNormal, coverage));
    }
    if (textures.has(decalOrm))
    {
        const float3 orm = textures.read(decalOrm, uv, uvDx, uvDy).rgb;
        layer.occlusion = lerp(layer.occlusion, orm.r, coverage);
        layer.roughness = clamp(lerp(layer.roughness, orm.g, coverage), 0.045, 1.0);
        layer.metallic = lerp(layer.metallic, orm.b, coverage);
    }
    if (textures.has(decalEmission))
    {
        layer.emission += textures.read(decalEmission, uv, uvDx, uvDy).rgb * decal.modulate.rgb * decal.emissionEnergy * fade;
    }
}
