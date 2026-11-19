// Precomputes the light of the environment for OpenGL, translated into GLSL, as ibl.slang does with compute shaders,
// which OpenGL ES 3.0 and WebGL2 lack: each face of each cube, and the BRDF table, is drawn by a triangle over it, whose
// pixels are the texels ibl.slang writes. The environment as a cube, its prefiltered specular reflections for each
// roughness, its diffuse irradiance, and the split-sum BRDF table.

struct BakeData
{
    // Width of the face or the table drawn into.
    uint size;
    float roughness;
    uint sampleCount;
    // Level of detail to read the equirectangular map at.
    float sourceLod;
    // Width of a face of the radiance cube.
    float sourceSize;
    // The face drawn into, in Vulkan's order.
    uint face;
    uint padding0;
    uint padding1;
};

ConstantBuffer<BakeData> bake;
Sampler2D equirectangular;
SamplerCube radiance;

static const float pi = 3.14159265358979;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput bakeVertex(uint vertexIndex : SV_VulkanVertexID)
{
    const float2 uv = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    VertexOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Direction through the center of a texel of a cube face, following the Vulkan face order. OpenGL counts the rows of what
// it draws into from the first stored, as Vulkan's compute shaders count them.
float3 cubeDirection(uint face, float2 texel, uint size)
{
    const float2 uv = texel / float(size) * 2.0 - 1.0;
    switch (face)
    {
    case 0:
        return normalize(float3(1.0, -uv.y, -uv.x));
    case 1:
        return normalize(float3(-1.0, -uv.y, uv.x));
    case 2:
        return normalize(float3(uv.x, 1.0, uv.y));
    case 3:
        return normalize(float3(uv.x, -1.0, -uv.y));
    case 4:
        return normalize(float3(uv.x, -uv.y, 1.0));
    default:
        return normalize(float3(-uv.x, -uv.y, -1.0));
    }
}

float radicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), radicalInverse(index));
}

// An orthonormal basis around a normal.
float3x3 tangentFrame(float3 normal)
{
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

// A half vector distributed by GGX around +Z.
float3 sampleGgx(float2 xi, float alpha)
{
    const float phi = 2.0 * pi * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float distributionGgx(float NoH, float alpha)
{
    const float a2 = alpha * alpha;
    const float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (pi * f * f);
}

float2 equirectangularUv(float3 direction)
{
    const float u = 0.5 + atan2(direction.x, -direction.z) / (2.0 * pi);
    const float v = acos(clamp(direction.y, -1.0, 1.0)) / pi;
    return float2(u, v);
}

// Mip level whose texels cover the solid angle of a sample, which filters the environment instead of taking many more
// samples (filtered importance sampling).
float sampleLod(float pdf)
{
    const float sampleSolidAngle = 1.0 / (float(bake.sampleCount) * max(pdf, 1e-6));
    const float texelSolidAngle = 4.0 * pi / (6.0 * bake.sourceSize * bake.sourceSize);
    return max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
}

[shader("fragment")]
float4 equirectangularToCube(VertexOutput input) : SV_Target
{
    const float3 direction = cubeDirection(bake.face, input.position.xy, bake.size);
    return float4(equirectangular.SampleLevel(equirectangularUv(direction), bake.sourceLod).rgb, 1.0);
}

[shader("fragment")]
float4 prefilterSpecular(VertexOutput input) : SV_Target
{
    const float3 normal = cubeDirection(bake.face, input.position.xy, bake.size);
    if (bake.roughness <= 0.0)
    {
        return float4(radiance.SampleLevel(normal, 0.0).rgb, 1.0);
    }
    // The view and reflection directions are assumed equal to the normal.
    const float alpha = bake.roughness * bake.roughness;
    const float3x3 frame = tangentFrame(normal);
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint index = 0; index < bake.sampleCount; ++index)
    {
        const float3 halfway = mul(sampleGgx(hammersley(index, bake.sampleCount), alpha), frame);
        const float3 light = reflect(-normal, halfway);
        const float NoL = dot(normal, light);
        if (NoL <= 0.0)
        {
            continue;
        }
        const float NoH = saturate(dot(normal, halfway));
        const float pdf = distributionGgx(NoH, alpha) * 0.25;
        sum += radiance.SampleLevel(light, sampleLod(pdf)).rgb * NoL;
        weight += NoL;
    }
    return float4(sum / max(weight, 1e-6), 1.0);
}

[shader("fragment")]
float4 irradiance(VertexOutput input) : SV_Target
{
    const float3 normal = cubeDirection(bake.face, input.position.xy, bake.size);
    const float3x3 frame = tangentFrame(normal);
    // Cosine-weighted directions: the average radiance is the irradiance divided by pi, which a Lambertian surface of
    // albedo 1 reflects.
    float3 sum = float3(0.0);
    for (uint index = 0; index < bake.sampleCount; ++index)
    {
        const float2 xi = hammersley(index, bake.sampleCount);
        const float phi = 2.0 * pi * xi.x;
        const float cosTheta = sqrt(1.0 - xi.y);
        const float sinTheta = sqrt(xi.y);
        const float3 local = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        sum += radiance.SampleLevel(mul(local, frame), sampleLod(cosTheta / pi)).rgb;
    }
    return float4(sum / float(bake.sampleCount), 1.0);
}

float visibilitySmithGgxCorrelated(float NoV, float NoL, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambdaV = NoL * sqrt((NoV - NoV * a2) * NoV + a2);
    const float lambdaL = NoV * sqrt((NoL - NoL * a2) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

// Scale and bias of the Fresnel reflectance at normal incidence, indexed by the cosine between normal and view and by
// roughness.
[shader("fragment")]
float4 brdfTable(VertexOutput input) : SV_Target
{
    const float NoV = max(input.position.x / float(bake.size), 1e-4);
    const float roughness = input.position.y / float(bake.size);
    const float alpha = roughness * roughness;
    const float3 view = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    float scale = 0.0;
    float bias = 0.0;
    for (uint index = 0; index < bake.sampleCount; ++index)
    {
        const float3 halfway = sampleGgx(hammersley(index, bake.sampleCount), alpha);
        const float3 light = reflect(-view, halfway);
        const float NoL = saturate(light.z);
        const float NoH = saturate(halfway.z);
        const float VoH = saturate(dot(view, halfway));
        if (NoL <= 0.0)
        {
            continue;
        }
        const float visibility = visibilitySmithGgxCorrelated(NoV, NoL, alpha) * 4.0 * NoL * VoH / max(NoH, 1e-6);
        const float fresnel = pow(1.0 - VoH, 5.0);
        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }
    return float4(scale, bias, 0.0, 1.0) / float(bake.sampleCount);
}

// Measures the exposed luminance of the scene on a coarse grid, as luminance.slang does: each pixel of the grid reads the
// scene where it falls, which the CPU reads back to adapt the exposure.
Sampler2D sceneTexture;

[shader("fragment")]
float4 luminanceFragment(VertexOutput input) : SV_Target
{
    const float2 uv = input.position.xy / float2(float(bake.size), bake.sourceSize);
    const float3 color = sceneTexture.SampleLevel(uv, 0.0).rgb;
    return float4(max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0), 0.0, 0.0, 1.0);
}
