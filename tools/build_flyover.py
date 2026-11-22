"""Open decoded, already positioned models in a standalone Blender scene."""

import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Quaternion, Vector


def load_image(path, cache):
    if path not in cache:
        image = bpy.data.images.load(str(path), check_existing=True)
        image.pack()
        material = bpy.data.materials.new(path.stem)
        material.use_nodes = True
        nodes = material.node_tree.nodes
        texture = nodes.new("ShaderNodeTexImage")
        texture.image = image
        surface = nodes.get("Principled BSDF")
        material.node_tree.links.new(texture.outputs["Color"], surface.inputs["Base Color"])
        nodes.active = texture
        cache[path] = material
    return cache[path]


def import_mesh(path):
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=str(path), forward_axis="NEGATIVE_Y", up_axis="Z",
                          use_split_groups=False, use_split_objects=True)
    return [obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"]


def place_model(root, row, materials):
    model, entity = row["model"], row["entity"]
    name = model["Name"]
    quaternion = entity["rotation"]
    scale = (entity["scale_xy"], entity["scale_xy"], entity["scale_z"])
    if any(not math.isfinite(value) for value in (*quaternion, *scale, *entity["position"])):
        raise ValueError(f"Invalid transform for {name}")
    rotation = Quaternion((quaternion[3], *quaternion[:3]))
    if rotation.magnitude == 0:
        raise ValueError(f"Empty rotation for {name}")
    rotation.normalize()
    path = root / row["directory"] / model["File"]
    model_cache = materials.setdefault("__models__", {})
    cache_key = str(path.resolve())
    prototypes = model_cache.get(cache_key)
    first_instance = prototypes is None
    textured = 0
    if prototypes is None:
        with path.open(encoding="utf-8") as source:
            mesh_names = [line[2:].strip() for line in source if line.startswith("o ")]
        shaders = dict(zip(mesh_names, model["MeshShaders"]))
        base_textures = {
            material["ShaderIndex"]: ref["Id64"].upper()
            for material in (model["Materials"] or [])
            for ref in material["Textures"]
            if ref["Param"] == "C8E8C282"  # diffusetex
        }
        imported = import_mesh(path)
        prototypes = []
        for obj in imported:
            mesh_name = obj.name
            obj.name = f"{name} · {mesh_name}"
            asset_key = base_textures.get(shaders.get(mesh_name))
            texture = root / "textures" / (str(asset_key) + ".png")
            if asset_key and texture.is_file():
                obj.data.materials.clear()
                obj.data.materials.append(load_image(texture, materials))
                textured += 1
            prototypes.append((mesh_name, obj))
        model_cache[cache_key] = prototypes

    objects = []
    for mesh_name, prototype in prototypes:
        if first_instance:
            obj = prototype
        else:
            obj = bpy.data.objects.new(f"{name} · {mesh_name}", prototype.data)
            bpy.context.collection.objects.link(obj)
            textured += int(bool(prototype.data.materials))
        objects.append(obj)
    for obj in objects:
        obj.location = entity["position"]
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = rotation.conjugated()
        obj.scale = scale
        obj["archive_map_offset"] = f"0x{row['offset']:X}"
        obj["archive_model_offset"] = f"0x{model['Offset']:X}"
        obj["world_position"] = tuple(entity["position"])
    return len(objects), textured


def configure_view(rows):
    positions = [Vector(row["entity"]["position"]) for row in rows]
    low = Vector(tuple(min(pos[axis] for pos in positions) for axis in range(3)))
    high = Vector(tuple(max(pos[axis] for pos in positions) for axis in range(3)))
    view = (low + high) * 0.5
    distance = max(30, (high - low).length * 0.77)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type != "VIEW_3D":
                continue
            space = area.spaces.active
            space.clip_end = max(10000, distance * 10)
            space.overlay.show_overlays = False
            space.overlay.show_floor = False
            space.overlay.show_axis_x = False
            space.overlay.show_axis_y = False
            space.shading.color_type = "TEXTURE"
            space.region_3d.view_location = view
            space.region_3d.view_distance = distance
            space.region_3d.view_rotation = Vector((0.3, -0.6, -0.68)).to_track_quat("-Z", "Y")


def main():
    source, output = (Path(arg).resolve() for arg in sys.argv[sys.argv.index("--") + 1:])
    rows = json.loads(source.read_text(encoding="utf-8-sig"))
    if not rows:
        raise ValueError("The placement file has no matched models")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    materials = {"__models__": {}}
    counts = [place_model(source.parent, row, materials) for row in rows]
    if not sum(count[0] for count in counts):
        raise ValueError("No meshes were imported")
    configure_view(rows)
    bpy.context.scene["source"] = "Xbox build · levels.rpf"
    bpy.context.scene["placements"] = len(rows)
    bpy.context.scene["textured_mesh_objects"] = sum(count[1] for count in counts)
    guide = bpy.data.texts.new("Como voar")
    guide.write("Passe o rato sobre a vista 3D. Abra a pesquisa com F3 e escolha Fly Navigation.\n"
                "Use Esc para terminar o voo. Use Home para voltar a enquadrar os modelos.\n\n"
                "A cena contém apenas os modelos e as posições que foi possível ligar no arquivo local.\n"
                "O material aplica a textura difusa quando a sua chave foi confirmada no pacote.\n")
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    print(f"PLACEMENTS={len(rows)} MESHES={sum(count[0] for count in counts)} TEXTURED={sum(count[1] for count in counts)}")


if __name__ == "__main__":
    main()
