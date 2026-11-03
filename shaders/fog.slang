// Volumetric fog in froxels, cells of a grid that follows the view, as Godot's (after Wronski, 2014,
// and Hillaire, 2015): each cell gathers what the fog is made of there and the light it catches,
// mixed with the previous frames; then each column is integrated from the camera outwards into the
// light the fog sends towards the camera and the share of what is behind that it lets through.

import common;

struct FogPushConstants
{
    SceneData* scene;
    VolumetricFog* fog;
};

[[vk::push_constant]]
ConstantBuffer<FogPushConstants> constants;

// What a pass writes, and what it reads: the previous froxels when lighting, the lit ones when
// integrating.
[[vk::binding(0, 2)]]
RWTexture2DArray<float4> written;
[[vk::binding(1, 2)]]
Texture2DArray<float4> read;

// A point of the view: across the screen at uv, `distance` meters along the view.
float3 viewPoint(SceneData* scene, float2 uv, float distance)
{
    // Any point of the pixel's ray, then along the ray to the distance asked.
    const float4 onRay = mul(scene.inverseViewProjection, float4(uv * 2.0 - 1.0, 0.5, 1.0));
    const float3 point = onRay.xyz / onRay.w;
    const float pointDistance = max(-mul(scene.view, float4(point, 1.0)).z, 1e-6);
    return scene.cameraPosition + (point - scene.cameraPosition) * (distance / pointDistance);
}

// How much of the sun reaches a point of the air, through its cascades of shadows.
float sunVisibility(SceneData* scene, float3 position, float viewDistance)
{
    if ((scene.sunFlags & sunCastsShadows) == 0 || viewDistance >= scene.shadowDistance)
    {
        return 1.0;
    }
    for (uint cascade = 0; cascade < cascadeCount; ++cascade)
    {
        if (viewDistance < scene.cascadeSplits[cascade])
        {
            const float4 clip = mul(scene.cascadeViewProjections[cascade], float4(position, 1.0));
            const float2 uv = clip.xy * 0.5 + 0.5;
            if (any(uv < 0.0) || any(uv > 1.0) || clip.z > 1.0)
            {
                return 1.0;
            }
            return shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, float(cascade)), clip.z - 0.0005);
        }
    }
    return 1.0;
}

// The light the local lights of the cluster of a point send through it towards the camera, without
// their shadows.
float3 localLightsThrough(SceneData* scene, float3 position, float2 uv, float viewDistance, float3 toCamera, float anisotropy)
{
    if (scene.lightCount == 0)
    {
        return float3(0.0);
    }
    const uint tileX = min(uint(uv.x * scene.clusterCountX), scene.clusterCountX - 1);
    const uint tileY = min(uint(uv.y * scene.clusterCountY), scene.clusterCountY - 1);
    const float slice = floor(log2(max(viewDistance, 1e-4)) * scene.clusterSliceScale + scene.clusterSliceBias);
    const uint sliceIndex = uint(clamp(slice, 0.0, float(scene.clusterCountZ - 1)));
    const Cluster cluster = scene.clusters[(sliceIndex * scene.clusterCountY + tileY) * scene.clusterCountX + tileX];
    float3 light = float3(0.0);
    for (uint entry = 0; entry < cluster.count; ++entry)
    {
        const Light source = scene.lights[scene.clusterLights[cluster.offset + entry]];
        const float3 fromLight = position - source.position;
        const float distanceSquared = dot(fromLight, fromLight);
        const float3 direction = fromLight * rsqrt(max(distanceSquared, 1e-8));
        // As on surfaces: inverse square, windowed to end at the range, and the cone of a spot.
        const float ratio = distanceSquared / (source.range * source.range);
        const float window = saturate(1.0 - ratio * ratio);
        float attenuation = window * window / max(distanceSquared, 1e-4);
        const float spot = saturate(dot(source.direction, direction) * source.spotScale + source.spotOffset);
        attenuation *= spot * spot;
        light += source.intensity * attenuation * henyeyGreenstein(dot(direction, toCamera), anisotropy);
    }
    return light;
}

// Each froxel: what the fog is there, the light it catches and sends towards the camera, mixed with
// what the previous frames found at the same place.
[shader("compute")]
[numthreads(8, 8, 1)]
void lightFroxels(uint3 id : SV_DispatchThreadID)
{
    SceneData* scene = constants.scene;
    VolumetricFog* fog = constants.fog;
    if (id.x >= fog.width || id.y >= fog.height || id.z >= fog.depth)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(fog.width, fog.height);
    // Somewhere along the froxel, a different place each frame, which the reprojection averages.
    const float viewDistance = fogSliceDistance(fog, (float(id.z) + fog.jitter) / float(fog.depth));
    const float3 position = viewPoint(scene, uv, viewDistance);

    // The fog everywhere, then the volumes adding or carving theirs.
    float extinction = fog.density;
    float3 scattering = fog.albedo * fog.density;
    float3 emission = fog.emission;
    for (uint index = 0; index < fog.volumeCount; ++index)
    {
        const FogVolume volume = fog.volumes[index];
        const float3 local = mul(volume.worldToVolume, float4(position, 1.0)).xyz;
        const float edge = volume.shape == 0 ? max(abs(local.x), max(abs(local.y), abs(local.z))) : length(local);
        float weight = volume.shape == 2 ? 1.0 : saturate((1.0 - edge) / max(volume.edgeFade, 1e-4));
        if (volume.heightFalloff > 0.0)
        {
            weight *= exp(-volume.heightFalloff * max(position.y - volume.centerHeight, 0.0));
        }
        extinction += volume.density * weight;
        scattering += volume.albedo * volume.density * weight;
        emission += volume.emission * weight;
    }
    extinction = max(extinction, 0.0);
    scattering = max(scattering, float3(0.0));

    // The sun through its shadows, the local lights, and the sky lighting the fog evenly.
    // Back along the ray rather than from the place: the first slice may be sampled micrometers from
    // the camera, where rounding turns the direction anywhere and the phase with it.
    const float3 toCamera = normalize(scene.cameraPosition - viewPoint(scene, uv, 1.0));
    float3 caught = scene.sunIlluminance * sunVisibility(scene, position, viewDistance) *
                    henyeyGreenstein(dot(scene.sunDirection, toCamera), fog.anisotropy);
    caught += localLightsThrough(scene, position, uv, viewDistance, toCamera, fog.anisotropy);
    const float3 sky = scene.environmentColor * scene.environmentIntensity;
    const float3 skyIrradiance = (irradianceEnvironment.SampleLevel(clampSampler, float3(0.0, 1.0, 0.0), 0.0).rgb +
                                  irradianceEnvironment.SampleLevel(clampSampler, float3(0.0, -1.0, 0.0), 0.0).rgb) *
                                 0.5 * sky;
    caught += skyIrradiance / pi * fog.ambientInject;
    float4 current = float4(scattering * caught + emission, extinction);

    // Where this place was in the froxels of the previous frame.
    if (fog.temporalAmount > 0.0)
    {
        const float4 before = mul(scene.previousViewProjection, float4(position, 1.0));
        const float2 beforeUv = before.xy / max(before.w, 1e-6) * 0.5 + 0.5;
        const float beforeLayer = fogSliceShare(fog, before.w) * float(fog.depth) - 0.5;
        if (before.w > 0.0 && all(beforeUv >= 0.0) && all(beforeUv <= 1.0) && beforeLayer <= float(fog.depth) - 0.5)
        {
            const float lower = clamp(floor(beforeLayer), 0.0, float(fog.depth) - 1.0);
            const float upper = min(lower + 1.0, float(fog.depth) - 1.0);
            const float4 history = lerp(read.SampleLevel(clampSampler, float3(beforeUv, lower), 0.0),
                                        read.SampleLevel(clampSampler, float3(beforeUv, upper), 0.0), saturate(beforeLayer - lower));
            current = lerp(current, history, fog.temporalAmount);
        }
    }
    written[id] = current;
}

// Each column, from the camera outwards: the light gathered so far and the share let through, at the
// far end of every slice.
[shader("compute")]
[numthreads(8, 8, 1)]
void integrateFroxels(uint3 id : SV_DispatchThreadID)
{
    SceneData* scene = constants.scene;
    VolumetricFog* fog = constants.fog;
    if (id.x >= fog.width || id.y >= fog.height)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(fog.width, fog.height);
    // Meters along the ray for a meter along the view, wider at the sides of the screen.
    const float rayScale = length(viewPoint(scene, uv, 1.0) - scene.cameraPosition);
    float3 gathered = float3(0.0);
    float through = 1.0;
    float near = 0.0;
    for (uint slice = 0; slice < fog.depth; ++slice)
    {
        const float far = fogSliceDistance(fog, float(slice + 1) / float(fog.depth));
        const float segment = (far - near) * rayScale;
        const float4 froxel = read.Load(int4(id.xy, slice, 0));
        // Energy conserving across the segment, as Hillaire's: the light scattered along it, each part
        // dimmed by what comes before it.
        const float passing = exp(-froxel.a * segment);
        const float3 scattered = froxel.a > 1e-6 ? froxel.rgb * (1.0 - passing) / froxel.a : froxel.rgb * segment;
        gathered += through * scattered;
        through *= passing;
        written[int3(id.xy, slice)] = float4(gathered, through);
        near = far;
    }
}
