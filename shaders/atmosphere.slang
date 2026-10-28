// The tables of the atmosphere, after Hillaire, "A Scalable and Production Ready Sky and Atmosphere
// Rendering Technique" (2020): what the air lets through, the light scattered many times, the sky
// around the camera and the haze in front of it; then the sky as a small cube map, prefiltered for
// the roughness of the surfaces that reflect it and convolved for their diffuse light. Lengths in
// kilometers, the planet centered below the camera, Y up.

import common;

struct AtmospherePass
{
    Atmosphere* air;
    SceneData* scene;
    // Width of the faces written, the roughness they are filtered for, and the samples it takes.
    uint size;
    float roughness;
    uint sampleCount;
    uint padding;
};

[[vk::push_constant]]
ConstantBuffer<AtmospherePass> pass;

[[vk::binding(0, 2)]]
RWTexture2D<float4> tableTarget;
[[vk::binding(1, 2)]]
RWTexture2DArray<float4> layersTarget;
[[vk::binding(2, 2)]]
Texture2D multiScatteringTable;
[[vk::binding(3, 2)]]
TextureCube skyCubeSource;

// What the air holds at a point: its scattering, by the molecules and the aerosols, and how much it
// stops in all, per kilometer.
struct Medium
{
    float3 rayleigh;
    float mie;
    float3 scattering;
    float3 extinction;
};

Medium mediumAt(Atmosphere* air, float3 position)
{
    const float height = max(length(position) - air.bottomRadius, 0.0);
    const float rayleighDensity = exp(-height / max(air.rayleighHeight, 1e-3));
    const float mieDensity = exp(-height / max(air.mieHeight, 1e-3));
    // The ozone, in a layer thickest at 25 km and gone at 10 and 40.
    const float ozoneDensity = saturate(1.0 - abs(height - 25.0) / 15.0);
    Medium medium;
    medium.rayleigh = air.rayleighScattering * rayleighDensity;
    medium.mie = air.mieScattering * mieDensity;
    medium.scattering = medium.rayleigh + medium.mie;
    medium.extinction = medium.rayleigh + (air.mieScattering + air.mieAbsorption) * mieDensity + air.ozoneAbsorption * ozoneDensity;
    return medium;
}

float rayleighPhase(float cosAngle)
{
    return 3.0 / (16.0 * pi) * (1.0 + cosAngle * cosAngle);
}

// Cornette-Shanks: the aerosols throw the light forward, around the sun.
float miePhase(float cosAngle, float g)
{
    const float g2 = g * g;
    const float k = 3.0 / (8.0 * pi) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + cosAngle * cosAngle) / pow(max(1.0 + g2 - 2.0 * g * cosAngle, 1e-4), 1.5);
}

// The light scattered more than once that reaches a point, per unit of scattering and of sun.
float3 multipleScattering(Atmosphere* air, float3 position, float cosSunZenith)
{
    const float2 uv = float2(cosSunZenith * 0.5 + 0.5,
                             saturate((length(position) - air.bottomRadius) / (air.topRadius - air.bottomRadius)));
    return multiScatteringTable
        .SampleLevel(clampSampler, float2(unitToTexels(uv.x, multiScatteringTableSize.x), unitToTexels(uv.y, multiScatteringTableSize.y)), 0.0)
        .rgb;
}

struct Scattered
{
    // Towards the origin, per unit of sun.
    float3 luminance;
    // What the air lets through from the end of the ray.
    float3 transmittance;
    // The share of the light scattered once more along the ray, for the multiple scattering.
    float3 scatteredOnce;
};

// The light the air scatters towards `origin` along a ray, up to `farthest` kilometers: the sun
// seen through the air above each point, unless the planet hides it, turned by the phase of the
// molecules and aerosols (or evenly), plus what was scattered many times, and the lit ground where
// the ray meets it.
Scattered integrate(Atmosphere* air, float3 origin, float3 direction, float3 sun, uint samples, float farthest, bool phased,
                    bool multiple, bool ground)
{
    Scattered result;
    result.luminance = float3(0.0);
    result.transmittance = float3(1.0);
    result.scatteredOnce = float3(0.0);
    const float toTop = raySphereNearest(origin, direction, air.topRadius);
    if (toTop < 0.0)
    {
        return result;
    }
    const float toGround = raySphereNearest(origin, direction, air.bottomRadius);
    const float distance = min(toGround >= 0.0 ? min(toGround, toTop) : toTop, farthest);
    const float cosAngle = dot(direction, sun);
    const float step = distance / float(samples);
    float3 throughput = float3(1.0);
    for (uint index = 0; index < samples; ++index)
    {
        const float3 position = origin + direction * (float(index) + 0.3) * step;
        const Medium medium = mediumAt(air, position);
        const float3 stepTransmittance = exp(-medium.extinction * step);
        const float height = length(position);
        const float3 up = position / height;
        const float cosSunZenith = dot(sun, up);
        const float3 throughAir = transmittanceTowards(air, height, cosSunZenith);
        const float shadowed = raySphereNearest(position + up * 0.01, sun, air.bottomRadius) >= 0.0 ? 0.0 : 1.0;
        const float3 turned = phased ? medium.rayleigh * rayleighPhase(cosAngle) + medium.mie * miePhase(cosAngle, air.mieAnisotropy)
                                     : medium.scattering / (4.0 * pi);
        float3 source = throughAir * shadowed * turned;
        if (multiple)
        {
            source += multipleScattering(air, position, cosSunZenith) * medium.scattering;
        }
        // Integrated over the step, as the light fades through it.
        const float3 extinction = max(medium.extinction, float3(1e-7));
        result.luminance += throughput * (source - source * stepTransmittance) / extinction;
        result.scatteredOnce += throughput * (medium.scattering - medium.scattering * stepTransmittance) / extinction;
        throughput *= stepTransmittance;
    }
    if (ground && toGround >= 0.0 && toGround <= farthest)
    {
        const float3 position = origin + direction * toGround;
        const float3 up = normalize(position);
        const float cosSun = dot(sun, up);
        result.luminance += throughput * transmittanceTowards(air, length(position), cosSun) * saturate(cosSun) * air.groundAlbedo / pi;
    }
    result.transmittance = throughput;
    return result;
}

// What the air lets through from a height towards the top, for every height and direction.
[shader("compute")]
[numthreads(8, 8, 1)]
void transmittanceTable(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(transmittanceTableSize.x) || id.y >= uint(transmittanceTableSize.y))
    {
        return;
    }
    Atmosphere* air = pass.air;
    const float2 uv = (float2(id.xy) + 0.5) / transmittanceTableSize;
    const float horizon = sqrt(air.topRadius * air.topRadius - air.bottomRadius * air.bottomRadius);
    const float rho = horizon * uv.y;
    const float radius = sqrt(rho * rho + air.bottomRadius * air.bottomRadius);
    const float nearest = air.topRadius - radius;
    const float farthest = rho + horizon;
    const float distance = nearest + uv.x * (farthest - nearest);
    const float cosZenith =
        distance == 0.0 ? 1.0 : clamp((horizon * horizon - rho * rho - distance * distance) / (2.0 * radius * distance), -1.0, 1.0);
    const float3 origin = float3(0.0, radius, 0.0);
    const float3 direction = float3(sqrt(max(1.0 - cosZenith * cosZenith, 0.0)), cosZenith, 0.0);
    const float span = max(raySphereNearest(origin, direction, air.topRadius), 0.0);
    const uint samples = 40;
    float3 depth = float3(0.0);
    for (uint index = 0; index < samples; ++index)
    {
        depth += mediumAt(air, origin + direction * (float(index) + 0.5) / float(samples) * span).extinction;
    }
    tableTarget[id.xy] = float4(exp(-depth * span / float(samples)), 1.0);
}

// The light scattered twice and more, for every height and height of the sun: the second order
// gathered over the whole sphere, summed as a geometric series of its transfer.
[shader("compute")]
[numthreads(8, 8, 1)]
void multiScatteringTableMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(multiScatteringTableSize.x) || id.y >= uint(multiScatteringTableSize.y))
    {
        return;
    }
    Atmosphere* air = pass.air;
    const float2 texel = (float2(id.xy) + 0.5) / multiScatteringTableSize;
    const float2 uv = float2(texelsToUnit(texel.x, multiScatteringTableSize.x), texelsToUnit(texel.y, multiScatteringTableSize.y));
    const float cosSunZenith = uv.x * 2.0 - 1.0;
    const float radius = air.bottomRadius + saturate(uv.y) * (air.topRadius - air.bottomRadius) + 0.01;
    const float3 origin = float3(0.0, min(radius, air.topRadius - 0.01), 0.0);
    const float3 sun = float3(sqrt(max(1.0 - cosSunZenith * cosSunZenith, 0.0)), cosSunZenith, 0.0);
    float3 secondOrder = float3(0.0);
    float3 transfer = float3(0.0);
    const uint side = 8;
    for (uint i = 0; i < side; ++i)
    {
        for (uint j = 0; j < side; ++j)
        {
            const float theta = 2.0 * pi * (float(i) + 0.5) / float(side);
            const float phi = acos(1.0 - 2.0 * (float(j) + 0.5) / float(side));
            const float3 direction = float3(cos(theta) * sin(phi), cos(phi), sin(theta) * sin(phi));
            const Scattered scattered = integrate(air, origin, direction, sun, 20, 9000.0, false, false, true);
            secondOrder += scattered.luminance;
            transfer += scattered.scatteredOnce;
        }
    }
    // Each direction covers an equal share of the sphere, scattered evenly.
    secondOrder /= float(side * side);
    transfer /= float(side * side);
    tableTarget[id.xy] = float4(secondOrder / max(float3(1.0) - transfer, float3(1e-4)), 1.0);
}

// The light of the sky around the camera, in nits, for directions laid out as skyViewUv reads them.
[shader("compute")]
[numthreads(8, 8, 1)]
void skyViewTableMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(skyViewTableSize.x) || id.y >= uint(skyViewTableSize.y))
    {
        return;
    }
    Atmosphere* air = pass.air;
    const float2 texel = (float2(id.xy) + 0.5) / skyViewTableSize;
    const float2 uv = float2(texelsToUnit(texel.x, skyViewTableSize.x), texelsToUnit(texel.y, skyViewTableSize.y));
    const float radius = air.cameraRadius;
    const float horizon = sqrt(max(radius * radius - air.bottomRadius * air.bottomRadius, 0.0));
    const float beta = acos(clamp(horizon / radius, -1.0, 1.0));
    const float zenithHorizon = pi - beta;
    float viewZenith;
    if (uv.y < 0.5)
    {
        float coordinate = 1.0 - 2.0 * uv.y;
        coordinate = 1.0 - coordinate * coordinate;
        viewZenith = zenithHorizon * coordinate;
    }
    else
    {
        const float coordinate = uv.y * 2.0 - 1.0;
        viewZenith = zenithHorizon + beta * coordinate * coordinate;
    }
    const float cosViewZenith = cos(viewZenith);
    const float sinViewZenith = sin(viewZenith);
    const float across = saturate(uv.x);
    const float cosSunAzimuth = -(across * across * 2.0 - 1.0);
    const float cosSunZenith = air.sunDirection.y;
    const float3 sun = normalize(float3(sqrt(max(1.0 - cosSunZenith * cosSunZenith, 0.0)), cosSunZenith, 0.0));
    const float3 direction = float3(sinViewZenith * cosSunAzimuth, cosViewZenith,
                                    sinViewZenith * sqrt(max(1.0 - cosSunAzimuth * cosSunAzimuth, 0.0)));
    // Below the horizon, the lit ground of the planet through the air.
    const Scattered scattered = integrate(air, float3(0.0, radius, 0.0), direction, sun, 30, 9000.0, true, true, true);
    tableTarget[id.xy] = float4(scattered.luminance * air.sunIlluminance, 1.0);
}

// The haze between the camera and the slices of distance in front of it: what the air sends
// towards the camera, in nits, and the share of the light behind it stopped, by pixel of a coarse
// grid of the screen.
[shader("compute")]
[numthreads(8, 8, 1)]
void aerialPerspectiveTable(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(aerialSlices) || id.y >= uint(aerialSlices) || id.z >= uint(aerialSlices))
    {
        return;
    }
    Atmosphere* air = pass.air;
    SceneData* scene = pass.scene;
    const float2 uv = (float2(id.xy) + 0.5) / aerialSlices;
    const float4 far = mul(scene.inverseViewProjection, float4(uv * 2.0 - 1.0, 0.5, 1.0));
    const float3 direction = normalize(far.xyz / far.w - scene.cameraPosition);
    const float slice = (float(id.z) + 0.5) / aerialSlices;
    const float kilometers = slice * slice * air.aerialDistance;
    const Scattered scattered =
        integrate(air, float3(0.0, air.cameraRadius, 0.0), direction, air.sunDirection, 12, kilometers, true, true, false);
    layersTarget[id] = float4(scattered.luminance * air.sunIlluminance, 1.0 - dot(scattered.transmittance, float3(1.0 / 3.0)));
}

// Direction through the center of a texel of a cube face, following the Vulkan face order.
float3 cubeDirection(uint face, uint2 texel, uint size)
{
    const float2 uv = (float2(texel) + 0.5) / float(size) * 2.0 - 1.0;
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

// The sky as a cube map, without the sun, which the surfaces get as direct light.
[shader("compute")]
[numthreads(8, 8, 1)]
void skyCube(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pass.size || id.y >= pass.size)
    {
        return;
    }
    layersTarget[id] = float4(atmosphereSky(pass.air, cubeDirection(id.z, id.xy, pass.size)), 1.0);
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

float3x3 tangentFrame(float3 normal)
{
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    return float3x3(tangent, cross(normal, tangent), normal);
}

// The sky reflected by a surface of a roughness: GGX samples around each direction, the view
// taken along it as for the sky's own maps.
[shader("compute")]
[numthreads(8, 8, 1)]
void prefilterSky(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pass.size || id.y >= pass.size)
    {
        return;
    }
    const float3 normal = cubeDirection(id.z, id.xy, pass.size);
    const float3x3 frame = tangentFrame(normal);
    const float alpha = pass.roughness * pass.roughness;
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint index = 0; index < pass.sampleCount; ++index)
    {
        const float2 xi = float2(float(index) / float(pass.sampleCount), radicalInverse(index));
        const float phi = 2.0 * pi * xi.x;
        const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
        const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        const float3 half = mul(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), frame);
        const float3 light = reflect(-normal, half);
        const float NoL = dot(normal, light);
        if (NoL > 0.0)
        {
            sum += skyCubeSource.SampleLevel(clampSampler, light, 0.0).rgb * NoL;
            weight += NoL;
        }
    }
    layersTarget[id] = float4(sum / max(weight, 1e-4), 1.0);
}

// The light the sky gives a Lambertian surface facing each direction, as the irradiance maps hold
// it: the average radiance around it, weighted by the cosine.
[shader("compute")]
[numthreads(8, 8, 1)]
void irradianceSky(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pass.size || id.y >= pass.size)
    {
        return;
    }
    const float3 normal = cubeDirection(id.z, id.xy, pass.size);
    const float3x3 frame = tangentFrame(normal);
    float3 sum = float3(0.0);
    for (uint index = 0; index < pass.sampleCount; ++index)
    {
        const float2 xi = float2(float(index) / float(pass.sampleCount), radicalInverse(index));
        const float phi = 2.0 * pi * xi.x;
        const float cosTheta = sqrt(1.0 - xi.y);
        const float sinTheta = sqrt(xi.y);
        const float3 direction = mul(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), frame);
        sum += skyCubeSource.SampleLevel(clampSampler, direction, 0.0).rgb;
    }
    layersTarget[id] = float4(sum / float(pass.sampleCount), 1.0);
}
