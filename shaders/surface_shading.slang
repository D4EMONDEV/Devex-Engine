// The light a surface reflects, shared by the meshes of Vulkan (pbr.slang, geometry.slang) and of WebGPU
// (webgpu_mesh.slang): plain arithmetic, without resources.
module surface_shading;

static const float pi = 3.14159265358979;

public struct Surface
{
    public float3 position;
    public float3 normal;
    // Towards the viewer.
    public float3 view;
    public float3 diffuseColor;
    public float3 f0;
    // Perceptual roughness, and its square used by the distribution.
    public float roughness;
    public float alpha;
    public float occlusion;
    public float viewDistance;
    public float2 fragmentCoordinate;
};

public float distributionGgx(float NoH, float alpha)
{
    const float a2 = alpha * alpha;
    const float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (pi * f * f);
}

public float visibilitySmithGgxCorrelated(float NoV, float NoL, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambdaV = NoL * sqrt((NoV - NoV * a2) * NoV + a2);
    const float lambdaL = NoV * sqrt((NoL - NoL * a2) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

public float3 fresnelSchlick(float3 f0, float VoH)
{
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Reflected luminance for light arriving from `toLight` with an illuminance at normal incidence.
public float3 shadeLight(Surface surface, float3 toLight, float3 illuminance)
{
    const float NoL = saturate(dot(surface.normal, toLight));
    if (NoL <= 0.0)
    {
        return float3(0.0);
    }
    const float3 halfway = normalize(surface.view + toLight);
    const float NoV = abs(dot(surface.normal, surface.view)) + 1e-5;
    const float NoH = saturate(dot(surface.normal, halfway));
    const float VoH = saturate(dot(surface.view, halfway));

    const float3 fresnel = fresnelSchlick(surface.f0, VoH);
    const float3 specular = distributionGgx(NoH, surface.alpha) *
                            visibilitySmithGgxCorrelated(NoV, NoL, surface.alpha) * fresnel;
    const float3 diffuse = surface.diffuseColor / pi;
    return (diffuse + specular) * illuminance * NoL;
}

// How much of a local light reaches a point `toLight` away from it, before its shadows: an inverse square falloff,
// windowed so that the light ends exactly at its range, and the cone of a spot.
public float localAttenuation(float3 toLight, float range, float3 lightDirection, float spotScale, float spotOffset)
{
    const float distanceSquared = dot(toLight, toLight);
    const float3 direction = toLight * rsqrt(max(distanceSquared, 1e-8));
    const float ratio = distanceSquared / (range * range);
    const float window = saturate(1.0 - ratio * ratio);
    const float attenuation = window * window / max(distanceSquared, 1e-4);
    const float spot = saturate(dot(lightDirection, -direction) * spotScale + spotOffset);
    return attenuation * spot * spot;
}

// The normal of a surface through its normal map, from its normal, its tangent along the normal and the sign of its
// bitangent, and the red and green of the map, from 0 to 1. Tangent-space normal maps store X and Y; Z is
// reconstructed.
public float3 normalMapped(float3 normal, float3 tangent, float bitangentSign, float2 mapped, float normalScale)
{
    const float2 encoded = mapped * 2.0 - 1.0;
    const float2 tilt = encoded * normalScale;
    const float lengthSquared = dot(tangent, tangent);
    if (lengthSquared <= 1e-10)
    {
        return normal;
    }
    tangent *= rsqrt(lengthSquared);
    const float3 bitangent = cross(normal, tangent) * (bitangentSign < 0.0 ? -1.0 : 1.0);
    const float z = sqrt(saturate(1.0 - dot(encoded, encoded)));
    return normalize(tangent * tilt.x + bitangent * tilt.y + normal * z);
}

// The light of the environment a surface reflects: its irradiance, what it reflects prefiltered at its roughness, and
// the scale and bias of the split-sum table.
public float3 ambientLight(Surface surface, float3 irradiance, float3 prefiltered, float2 brdf)
{
    const float3 diffuse = surface.diffuseColor * irradiance;
    const float3 specular = prefiltered * (surface.f0 * brdf.x + brdf.y);
    return (diffuse + specular) * surface.occlusion;
}

// Rotates a direction around the vertical axis, to turn the environment.
public float3 rotateAroundY(float3 direction, float angle)
{
    const float c = cos(angle);
    const float s = sin(angle);
    return float3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
}

// Equirectangular coordinates of a direction: the image center looks along -Z, the top up.
public float2 equirectangularUv(float3 direction)
{
    const float u = 0.5 + atan2(direction.x, -direction.z) / (2.0 * pi);
    const float v = acos(clamp(direction.y, -1.0, 1.0)) / pi;
    return float2(u, v);
}

// Normals transform with the cofactor matrix, which stays correct under non-uniform scales. Its
// sign follows the determinant so that mirrored transforms keep outward normals.
public float3 transformNormal(float4x4 world, float3 normal)
{
    const float3x3 columns = transpose(float3x3(world));
    const float3 transformed = cross(columns[1], columns[2]) * normal.x + cross(columns[2], columns[0]) * normal.y +
                               cross(columns[0], columns[1]) * normal.z;
    return dot(columns[0], cross(columns[1], columns[2])) < 0.0 ? -transformed : transformed;
}

// -1 for a transform that mirrors, which turns the bitangents of tangents around.
public float handedness(float4x4 world)
{
    const float3x3 columns = transpose(float3x3(world));
    return sign(dot(columns[0], cross(columns[1], columns[2])) + 1e-12);
}

// The face of the cube around a point light that a direction falls in, in the order the renderer
// writes them: +X, -X, +Y, -Y, +Z, -Z.
public uint cubeFace(float3 direction)
{
    const float3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return direction.x > 0.0 ? 0 : 1;
    }
    if (magnitude.y >= magnitude.z)
    {
        return direction.y > 0.0 ? 2 : 3;
    }
    return direction.z > 0.0 ? 4 : 5;
}
