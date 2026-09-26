# Builds the FBX and OBJ files of the importer tests with Blender (5.2 made those of the repository):
#   blender -b --factory-startup --python make_test_files.py -- tests/data/fbx
import math
import os
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = 24
    scene.frame_start = 1
    scene.frame_end = 25


def principled(material):
    if material.node_tree is None:
        material.use_nodes = True
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return material.node_tree.nodes.new("ShaderNodeBsdfPrincipled")


def checker_image(name, path):
    image = bpy.data.images.new(name, 4, 4, alpha=True)
    pixels = []
    for y in range(4):
        for x in range(4):
            on = (x + y) % 2 == 0
            pixels += [1.0, 1.0, 1.0, 1.0] if on else [0.1, 0.4, 0.9, 1.0]
    image.pixels = pixels
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    return image


def crate(image_path):
    """A 2 m cube standing 3 m up, turned a quarter around the vertical, half painted, half textured."""
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 3.0), rotation=(0.0, 0.0, math.radians(90.0)))
    crate = bpy.context.object
    crate.name = "Crate"
    crate.data.name = "Crate"

    painted = bpy.data.materials.new("Painted")
    bsdf = principled(painted)
    bsdf.inputs["Base Color"].default_value = (0.8, 0.2, 0.1, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.4
    bsdf.inputs["Metallic"].default_value = 0.0

    checker = bpy.data.materials.new("Checker")
    bsdf = principled(checker)
    texture = checker.node_tree.nodes.new("ShaderNodeTexImage")
    texture.image = checker_image("crate_color", image_path)
    checker.node_tree.links.new(texture.outputs["Color"], bsdf.inputs["Base Color"])

    crate.data.materials.append(painted)
    crate.data.materials.append(checker)
    for index, polygon in enumerate(crate.data.polygons):
        polygon.material_index = 0 if index < 3 else 1
    return crate


# crate.fbx: the texture embedded in the file.
reset()
crate(os.path.join(out, "crate_color.png"))
bpy.ops.export_scene.fbx(
    filepath=os.path.join(out, "crate.fbx"),
    object_types={"MESH"},
    embed_textures=True,
    path_mode="COPY",
    bake_anim=False,
    axis_forward="-Z",
    axis_up="Y",
)

# crate.obj: the same crate, its materials in crate.mtl and its texture beside it.
bpy.ops.wm.obj_export(
    filepath=os.path.join(out, "crate.obj"),
    export_materials=True,
    path_mode="RELATIVE",
    forward_axis="NEGATIVE_Z",
    up_axis="Y",
)

# walker.fbx: a leg of two bones, whose knee bends in the "Walk" animation.
reset()
bpy.ops.object.armature_add(location=(0.0, 0.0, 0.0))
rig = bpy.context.object
rig.name = "Rig"
rig.data.name = "Rig"
bpy.ops.object.mode_set(mode="EDIT")
bones = rig.data.edit_bones
hip = bones[0]
hip.name = "Hip"
hip.head = (0.0, 0.0, 2.0)
hip.tail = (0.0, 0.0, 1.0)
knee = bones.new("Knee")
knee.head = (0.0, 0.0, 1.0)
knee.tail = (0.0, 0.0, 0.0)
knee.parent = hip
knee.use_connect = True
bpy.ops.object.mode_set(mode="OBJECT")

# A column of three square rings, at the foot, the knee and the hip.
vertices = []
for z in (0.0, 1.0, 2.0):
    vertices += [(-0.2, -0.2, z), (0.2, -0.2, z), (0.2, 0.2, z), (-0.2, 0.2, z)]
faces = [(0, 3, 2, 1), (8, 9, 10, 11)]
for ring in range(2):
    base = ring * 4
    for side in range(4):
        a = base + side
        b = base + (side + 1) % 4
        faces.append((a, b, b + 4, a + 4))
mesh = bpy.data.meshes.new("Leg")
mesh.from_pydata(vertices, [], faces)
mesh.update()
leg = bpy.data.objects.new("Leg", mesh)
bpy.context.scene.collection.objects.link(leg)
skin = bpy.data.materials.new("Skin")
principled(skin).inputs["Base Color"].default_value = (0.9, 0.7, 0.6, 1.0)
mesh.materials.append(skin)

hip_group = leg.vertex_groups.new(name="Hip")
knee_group = leg.vertex_groups.new(name="Knee")
hip_group.add([8, 9, 10, 11], 1.0, "REPLACE")
knee_group.add([0, 1, 2, 3], 1.0, "REPLACE")
hip_group.add([4, 5, 6, 7], 0.5, "REPLACE")
knee_group.add([4, 5, 6, 7], 0.5, "REPLACE")
leg.parent = rig
modifier = leg.modifiers.new("Armature", "ARMATURE")
modifier.object = rig

pose_knee = rig.pose.bones["Knee"]
pose_knee.rotation_mode = "QUATERNION"
pose_knee.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
pose_knee.keyframe_insert("rotation_quaternion", frame=1)
half = math.radians(45.0) / 2.0
pose_knee.rotation_quaternion = (math.cos(half), math.sin(half), 0.0, 0.0)
pose_knee.keyframe_insert("rotation_quaternion", frame=25)
rig.animation_data.action.name = "Walk"

bpy.ops.export_scene.fbx(
    filepath=os.path.join(out, "walker.fbx"),
    object_types={"ARMATURE", "MESH"},
    add_leaf_bones=False,
    bake_anim=True,
    bake_anim_use_all_actions=True,
    bake_anim_use_nla_strips=False,
    bake_anim_force_startend_keying=True,
    axis_forward="-Z",
    axis_up="Y",
)
print("made", sorted(os.listdir(out)))
