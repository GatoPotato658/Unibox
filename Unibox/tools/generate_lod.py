import argparse
from collections import Counter, defaultdict
from math import dist, isfinite
from pathlib import Path
from struct import Struct, error as StructError, pack_into, unpack_from


VTX_HEADER = Struct('<iiHHiiiiii')
VTX_BODY = Struct('<ii')
VTX_MODEL = Struct('<ii')
VTX_LOD = Struct('<iif')
VTX_MESH = Struct('<iiB')
VTX_GROUP = Struct('<iiiiiiB')
VTX_STRIP = Struct('<iiiihBii')
VTX_VERTEX = Struct('<3BBH3B')
VTX_MATERIAL_REPLACEMENTS = Struct('<ii')
MDL_BODY = Struct('<iiii')
MDL_MODEL_SIZE = 148
MDL_MESH_SIZE = 116
MDL_MESH_LOD_COUNTS = 52
VVD_VERTEX_SIZE = 48


def unpack(fmt, data, offset):
    if offset < 0:
        raise ValueError('Negative model offset')
    return unpack_from(fmt, data, offset)


def table(data, start, count, stride):
    if start < 0 or count < 0 or start + count * stride > len(data):
        raise ValueError('Model table lies outside the file')
    return range(start, start + count * stride, stride)


def vertex_positions(vvd):
    count, = unpack('<i', vvd, 16)
    fixup_count, fixup_offset, vertex_offset = unpack('<iii', vvd, 48)
    positions = [unpack('<3f', vvd, offset + 16)
                 for offset in table(vvd, vertex_offset, count, VVD_VERTEX_SIZE)]
    if any(not all(map(isfinite, vertex)) for vertex in positions):
        raise ValueError('Non-finite vertex coordinates')
    if not fixup_count:
        return positions
    ordered = []
    for offset in table(vvd, fixup_offset, fixup_count, 12):
        lod, start, length = unpack('<iii', vvd, offset)
        if not 0 <= lod < 8 or min(start, length) < 0 or start + length > count:
            raise ValueError('Invalid VVD fixup')
        ordered.extend(positions[start:start + length])
    if len(ordered) != count:
        raise ValueError('VVD fixups do not cover the vertex data')
    return ordered


def normal(triangle, positions):
    a, b, c = (positions[index] for index in triangle)
    ux, uy, uz = (b[i] - a[i] for i in range(3))
    vx, vy, vz = (c[i] - a[i] for i in range(3))
    return uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx


def simplify(triangles, positions, bones, target, keep_boundaries):
    faces = dict(enumerate(triangles))
    incident = defaultdict(set)
    edges = Counter()
    for idx, triangle in faces.items():
        for vertex in triangle:
            incident[vertex].add(idx)
        a, b, c = sorted(triangle)
        edges.update(((a, b), (a, c), (b, c)))

    boundary = {vertex for edge, count in edges.items() if count == 1 for vertex in edge} if keep_boundaries else set()
    candidates = sorted((dist(positions[a], positions[b]), a, b)
                        for a, b in edges if bones[a] == bones[b]
                        and (a not in boundary or b not in boundary))
    for _, a, b in candidates:
        if len(faces) <= target:
            break
        shared = incident[a] & incident[b]
        if not 1 <= len(shared) <= 2 or len(faces) - len(shared) < target:
            continue
        neighbors_a = {vertex for idx in incident[a] for vertex in faces[idx]}
        neighbors_b = {vertex for idx in incident[b] for vertex in faces[idx]}
        opposite = {vertex for idx in shared for vertex in faces[idx]}
        if neighbors_a & neighbors_b != opposite or (a in boundary and b in boundary):
            continue
        if b in boundary or (a not in boundary and len(incident[a]) < len(incident[b])):
            a, b = b, a
        rewrites = {idx: tuple(a if vertex == b else vertex for vertex in faces[idx])
                    for idx in incident[b] - shared}
        existing = {frozenset(faces[idx]) for idx in incident[a] - shared}
        replacement = {frozenset(face) for face in rewrites.values()}
        if existing & replacement or len(replacement) != len(rewrites):
            continue
        if any(sum(old * new for old, new in zip(normal(faces[idx], positions), normal(face, positions))) <= 0
               for idx, face in rewrites.items()):
            continue
        for idx in shared:
            for vertex in faces.pop(idx):
                incident[vertex].remove(idx)
        faces.update(rewrites)
        incident[a].update(rewrites)
        incident[b].clear()
    return list(faces.values())


def simplify_group(data, result, group, positions, ratio, keep_boundaries):
    vert_count, vert_offset, index_count, index_offset, strip_count, strip_offset, _ = VTX_GROUP.unpack_from(data, group)
    vertices = [VTX_VERTEX.unpack_from(data, offset) for offset in
                table(data, group + vert_offset, vert_count, VTX_VERTEX.size)]
    indices_start = group + index_offset
    index_table = table(data, indices_start, index_count, 2)
    indices = [unpack('<H', data, offset)[0] for offset in index_table]
    strips = [(offset, VTX_STRIP.unpack_from(data, offset)) for offset in
              table(data, group + strip_offset, strip_count, VTX_STRIP.size)]
    if any(vertex[3] > 3 or vertex[4] >= len(positions) for vertex in vertices):
        raise ValueError('VTX vertex lies outside its MDL mesh')
    if any(strip[4] < 0 or strip[5] != 1 for _, strip in strips):
        raise ValueError('Only triangle-list strips are supported in the selected LOD')

    group_positions = [positions[vertex[4]] for vertex in vertices]
    group_bones = [vertex[5:5 + vertex[3]] for vertex in vertices]
    new_indices = []
    used_indices = 0
    for strip, (count, start, nverts, first_vertex, _, _, _, _) in strips:
        if (min(count, start, nverts, first_vertex) < 0 or count % 3 or start != used_indices
                or start + count > index_count or first_vertex + nverts > vert_count):
            raise ValueError('Invalid or non-contiguous triangle list')
        used_indices += count
        strip_indices = indices[start:start + count]
        if any(not first_vertex <= index < first_vertex + nverts for index in strip_indices):
            raise ValueError('VTX index lies outside its strip')
        triangles = list(zip(strip_indices[::3], strip_indices[1::3], strip_indices[2::3]))
        if any(len(set(triangle)) != 3 for triangle in triangles):
            raise ValueError('Degenerate triangle list in the selected LOD')
        target = max(1, round(len(triangles) * ratio))
        if len(triangles) > target:
            triangles = simplify(triangles, group_positions, group_bones, target, keep_boundaries)
        pack_into('<ii', result, strip, 3 * len(triangles), len(new_indices))
        new_indices.extend(index for triangle in triangles for index in triangle)

    if used_indices != index_count:
        raise ValueError('Strip index counts disagree with the strip group')
    pack_into('<i', result, group + 8, len(new_indices))
    pack_into(f'<{len(new_indices)}H', result, indices_start, *new_indices)
    return index_count // 3, len(new_indices) // 3


def generate(mdl, vvd, vtx, ratio, selected_lod, keep_boundaries):
    mdl_magic, mdl_version, checksum = unpack('<4sii', mdl, 0)
    vvd_magic, vvd_version, vvd_checksum = unpack('<4sii', vvd, 0)
    version, _, _, _, _, vtx_checksum, lod_count, _, body_count, body_offset = VTX_HEADER.unpack_from(vtx)
    if (mdl_magic, mdl_version, vvd_magic, vvd_version, version) != (b'IDST', 48, b'IDSV', 4, 7):
        raise ValueError('Expected MDL v48, VVD v4 and VTX v7')
    if checksum != vvd_checksum or checksum != vtx_checksum:
        raise ValueError('MDL, VVD and VTX do not belong to the same model')

    mdl_body_count, mdl_body_offset = unpack('<ii', mdl, 232)
    if mdl_body_count != body_count or not 1 <= lod_count <= 8:
        raise ValueError('Invalid body part or LOD count')
    positions = vertex_positions(vvd)
    result = bytearray(vtx)
    totals = defaultdict(lambda: [0, 0])
    for body, mdl_body in zip(table(vtx, body_offset, body_count, VTX_BODY.size),
                              table(mdl, mdl_body_offset, mdl_body_count, MDL_BODY.size)):
        model_count, model_offset = VTX_BODY.unpack_from(vtx, body)
        _, mdl_model_count, _, mdl_model_offset = MDL_BODY.unpack_from(mdl, mdl_body)
        if model_count != mdl_model_count:
            raise ValueError('Body part model counts disagree')
        for model, mdl_model in zip(table(vtx, body + model_offset, model_count, VTX_MODEL.size),
                                    table(mdl, mdl_body + mdl_model_offset, mdl_model_count, MDL_MODEL_SIZE)):
            model_lods, lod_offset = VTX_MODEL.unpack_from(vtx, model)
            mdl_mesh_count, mdl_mesh_offset, mdl_vert_count, vertex_index = unpack('<iiii', mdl, mdl_model + 72)
            if model_lods != lod_count or vertex_index < 0 or vertex_index % VVD_VERTEX_SIZE:
                raise ValueError('Invalid model LOD count or vertex offset')
            vertex_start = vertex_index // VVD_VERTEX_SIZE
            if mdl_vert_count < 0 or vertex_start + mdl_vert_count > len(positions):
                raise ValueError('Model vertices lie outside the VVD')
            lods = table(vtx, model + lod_offset, model_lods, VTX_LOD.size)
            visible = [index for index, lod in enumerate(lods) if VTX_LOD.unpack_from(vtx, lod)[2] >= 0]
            if not visible or (selected_lod is not None and selected_lod not in visible):
                raise ValueError('Requested LOD is absent or reserved for shadows')
            index = visible[-1] if selected_lod is None else selected_lod
            lod = lods[index]
            mesh_count, mesh_offset, _ = VTX_LOD.unpack_from(vtx, lod)
            if mesh_count != mdl_mesh_count:
                raise ValueError('LOD mesh count disagrees with the MDL')
            for mesh, mdl_mesh in zip(table(vtx, lod + mesh_offset, mesh_count, VTX_MESH.size),
                                       table(mdl, mdl_model + mdl_mesh_offset, mdl_mesh_count, MDL_MESH_SIZE)):
                group_count, group_offset, _ = VTX_MESH.unpack_from(vtx, mesh)
                num_vertices, mesh_vertex_offset = unpack('<ii', mdl, mdl_mesh + 8)
                if min(num_vertices, mesh_vertex_offset) < 0 or mesh_vertex_offset + num_vertices > mdl_vert_count:
                    raise ValueError('Mesh vertices lie outside the MDL model')
                start = vertex_start + mesh_vertex_offset
                mesh_positions = positions[start:start + num_vertices]
                for group in table(vtx, mesh + group_offset, group_count, VTX_GROUP.size):
                    before, after = simplify_group(vtx, result, group, mesh_positions, ratio, keep_boundaries)
                    totals[index][0] += before
                    totals[index][1] += after
    return result, totals


def reserve(data, size):
    offset = len(data)
    data.extend(bytes(size))
    return offset


def append_lod(mdl, vvd, vtx, simplified, distance):
    lod_count, material_offset, body_count, body_offset = unpack('<iiii', vtx, 20)
    vvd_lod_count, = unpack('<i', vvd, 12)
    if lod_count != 1 or vvd_lod_count != 1:
        raise ValueError('Appending requires an input model with exactly one LOD')

    material_count, replacements = VTX_MATERIAL_REPLACEMENTS.unpack_from(vtx, material_offset)
    table(vtx, material_offset + replacements, material_count, 6)
    result = bytearray(vtx[:VTX_HEADER.size])
    new_materials = reserve(result, 2 * VTX_MATERIAL_REPLACEMENTS.size)
    new_bodies = reserve(result, body_count * VTX_BODY.size)
    pack_into('<iiii', result, 20, 2, new_materials, body_count, new_bodies)
    lod_entries = []
    for index, body in enumerate(table(vtx, body_offset, body_count, VTX_BODY.size)):
        model_count, model_offset = VTX_BODY.unpack_from(vtx, body)
        new_body = new_bodies + index * VTX_BODY.size
        new_models = reserve(result, model_count * VTX_MODEL.size)
        VTX_BODY.pack_into(result, new_body, model_count, new_models - new_body)
        for model_index, model in enumerate(table(vtx, body + model_offset, model_count, VTX_MODEL.size)):
            _, lod_offset = VTX_MODEL.unpack_from(vtx, model)
            lod = model + lod_offset
            count, mesh_offset, switch = VTX_LOD.unpack_from(vtx, lod)
            new_model = new_models + model_index * VTX_MODEL.size
            new_lods = reserve(result, 2 * VTX_LOD.size)
            VTX_MODEL.pack_into(result, new_model, 2, new_lods - new_model)
            lod_entries.append((new_lods, count, lod + mesh_offset, switch))

    original_base = len(result)
    result.extend(vtx)
    reduced_base = len(result)
    result.extend(simplified)
    for index, base in enumerate((original_base, reduced_base)):
        entry = new_materials + index * VTX_MATERIAL_REPLACEMENTS.size
        VTX_MATERIAL_REPLACEMENTS.pack_into(result, entry, material_count, base + material_offset + replacements - entry)
    for entry, count, mesh, switch in lod_entries:
        VTX_LOD.pack_into(result, entry, count, original_base + mesh - entry, switch)
        entry += VTX_LOD.size
        VTX_LOD.pack_into(result, entry, count, reduced_base + mesh - entry, distance)

    new_mdl = bytearray(mdl)
    mdl_body_count, mdl_body_offset = unpack('<ii', mdl, 232)
    for body in table(mdl, mdl_body_offset, mdl_body_count, MDL_BODY.size):
        _, model_count, _, model_offset = MDL_BODY.unpack_from(mdl, body)
        for model in table(mdl, body + model_offset, model_count, MDL_MODEL_SIZE):
            mesh_count, mesh_offset = unpack('<ii', mdl, model + 72)
            for mesh in table(mdl, model + mesh_offset, mesh_count, MDL_MESH_SIZE):
                count, = unpack('<i', mdl, mesh + MDL_MESH_LOD_COUNTS)
                pack_into('<i', new_mdl, mesh + MDL_MESH_LOD_COUNTS + 4, count)

    new_vvd = bytearray(vvd)
    vertex_count, = unpack('<i', vvd, 16)
    pack_into('<i', new_vvd, 12, 2)
    pack_into('<i', new_vvd, 20, vertex_count)
    fixup_count, fixup_offset = unpack('<ii', vvd, 48)
    for fixup in table(vvd, fixup_offset, fixup_count, 12):
        pack_into('<i', new_vvd, fixup, 1)
    return new_mdl, new_vvd, result


def main():
    parser = argparse.ArgumentParser(description='Generate lower-poly triangle lists for a compiled Source model LOD.')
    parser.add_argument('model', type=Path, help='Source .mdl with adjacent .vvd and .dx90.vtx files')
    parser.add_argument('output', type=Path, nargs='?', help='New .dx90.vtx file, or a new folder with --append-lod. Omit to report counts only.')
    parser.add_argument('--ratio', type=float, default=0.25, help='Fraction of triangles to retain, default 0.25')
    parser.add_argument('--lod', type=int, help='Render LOD index. Defaults to the coarsest visible LOD.')
    parser.add_argument('--keep-boundaries', action='store_true', help='Keep mesh edges fixed to protect seams.')
    parser.add_argument('--append-lod', type=float, metavar='SWITCH_POINT',
                        help='Add a separate render tier to a single-LOD model at the given positive Source LOD switch point.')
    args = parser.parse_args()
    if not 0 < args.ratio < 1:
        parser.error('--ratio must be between 0 and 1, exclusive')
    if args.append_lod is not None and (args.lod is not None or not isfinite(args.append_lod) or args.append_lod <= 0):
        parser.error('--append-lod requires a positive finite switch point and cannot be combined with --lod')
    if args.model.suffix.lower() != '.mdl' or (args.output and args.append_lod is None and not args.output.name.lower().endswith('.dx90.vtx')):
        parser.error('Expected a .mdl input and a .dx90.vtx output')
    try:
        mdl = args.model.read_bytes()
        vvd = args.model.with_suffix('.vvd').read_bytes()
        vtx = args.model.with_suffix('.dx90.vtx').read_bytes()
        result, totals = generate(mdl, vvd, vtx, args.ratio, args.lod, args.keep_boundaries)
        reduced = any(before > after for before, after in totals.values())
        if args.append_lod is not None:
            new_mdl, new_vvd, new_vtx = append_lod(mdl, vvd, vtx, result, args.append_lod)
            if args.output and reduced:
                args.output.mkdir()
                for suffix, data in (('.mdl', new_mdl), ('.vvd', new_vvd), ('.dx90.vtx', new_vtx)):
                    with (args.output / args.model.with_suffix(suffix).name).open('xb') as stream:
                        stream.write(data)
            totals = {1: totals[0]} if 0 in totals else {}
        elif args.output and reduced:
            with args.output.open('xb') as stream:
                stream.write(result)
    except (OSError, ValueError, StructError, OverflowError) as exc:
        parser.exit(1, f'{exc}\n')
    if args.output and not reduced:
        print('No triangles could be removed. No output was written.')
    for lod, (before, after) in sorted(totals.items()):
        print(f'LOD {lod}: {before} -> {after} triangles')


if __name__ == '__main__':
    main()
