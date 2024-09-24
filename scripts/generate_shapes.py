import numpy as np
from pathlib import Path

def generate_square(size=1.0, clockwise=False):
    # Vertices for a square on the XY plane
    vertices = np.array([
        [-size / 2, -size / 2, 0],
        [size / 2, -size / 2, 0],
        [size / 2, size / 2, 0],
        [-size / 2, size / 2, 0]
    ])

    # Texture coordinates
    texcoords = np.array([
        [0, 0],
        [1, 0],
        [1, 1],
        [0, 1]
    ])

    # Face (two triangles to make a square)
    if clockwise:
        faces = np.array([
            [0, 1, 2],  # Triangle 1
            [0, 2, 3]   # Triangle 2
        ])
    else:
        faces = np.array([
            [0, 2, 1],  # Triangle 1 (counterclockwise)
            [0, 3, 2]   # Triangle 2 (counterclockwise)
        ])

    # Normals (all pointing in the z-direction)
    normals = np.array([
        [0, 0, 1],
        [0, 0, 1],
        [0, 0, 1],
        [0, 0, 1]
    ])

    return vertices, normals, texcoords, faces

def generate_mobius_strip(radius=1.0, width=0.1, segments=100, twists=1, clockwise=False):
    vertices = []
    normals = []
    texcoords = []
    faces = []

    for i in range(segments + 1):
        # Fraction of way around the circle
        theta = 2 * np.pi * i / segments
        x = radius * np.cos(theta)
        y = radius * np.sin(theta)

        for j in [-1, 1]:
            # Create width along the strip, twisted by a fraction of the circle
            offset = (j * width / 2) * np.sin(twists * theta)
            z = j * (width / 2) * np.cos(twists * theta)
            vertices.append([x + offset * np.cos(theta), y + offset * np.sin(theta), z])

            # Approximate normal
            normals.append([np.cos(theta), np.sin(theta), 0])

            # Texture coordinates
            texcoords.append([i / segments, (j + 1) / 2])

    # Faces (quads split into two triangles)
    for i in range(segments):
        v1 = i * 2
        v2 = v1 + 1
        v3 = v1 + 2
        v4 = v1 + 3

        if clockwise:
            faces.append([v1, v2, v4])  # Triangle 1 (reversed winding)
            faces.append([v1, v4, v3])  # Triangle 2 (reversed winding)
        else:
            faces.append([v1, v3, v4])  # Triangle 1
            faces.append([v1, v4, v2])  # Triangle 2

    return np.array(vertices), np.array(normals), np.array(texcoords), np.array(faces)

def generate_sphere(radius, subdivisions, clockwise=False):
    vertices = []
    normals = []
    texcoords = []
    faces = []

    for i in range(subdivisions + 1):
        lat = np.pi * i / subdivisions
        v = i / subdivisions  # Texture coordinate for latitude

        for j in range(subdivisions + 1):
            lon = 2 * np.pi * j / subdivisions
            u = j / subdivisions  # Texture coordinate for longitude

            x = radius * np.sin(lat) * np.cos(lon)
            y = radius * np.sin(lat) * np.sin(lon)
            z = radius * np.cos(lat)

            vertices.append([x, y, z])
            normals.append([x / radius, y / radius, z / radius])
            texcoords.append([u, 1 - v])  # Flip v to match OpenGL convention

    # Generate faces (as triangles)
    for i in range(subdivisions):
        for j in range(subdivisions):
            v1 = i * (subdivisions + 1) + j
            v2 = v1 + 1
            v3 = (i + 1) * (subdivisions + 1) + j
            v4 = v3 + 1
            if clockwise:
                faces.append([v1, v3, v2])  # Triangle 1 (reversed order)
                faces.append([v2, v3, v4])  # Triangle 2 (reversed order)
            else:
                faces.append([v1, v2, v3])  # Triangle 1
                faces.append([v2, v4, v3])  # Triangle 2

    return np.array(vertices), np.array(normals), np.array(texcoords), np.array(faces)


def generate_torus(outer_radius, inner_radius, radial_subdivisions, tubular_subdivisions, clockwise=False):
    vertices = []
    normals = []
    texcoords = []
    faces = []

    for i in range(radial_subdivisions):
        theta = 2 * np.pi * i / radial_subdivisions
        cos_theta = np.cos(theta)
        sin_theta = np.sin(theta)
        
        for j in range(tubular_subdivisions):
            phi = 2 * np.pi * j / tubular_subdivisions
            cos_phi = np.cos(phi)
            sin_phi = np.sin(phi)

            x = (outer_radius + inner_radius * cos_phi) * cos_theta
            y = (outer_radius + inner_radius * cos_phi) * sin_theta
            z = inner_radius * sin_phi

            vertices.append([x, y, z])

            nx = cos_phi * cos_theta
            ny = cos_phi * sin_theta
            nz = sin_phi
            normals.append([nx, ny, nz])

            u = i / radial_subdivisions
            v = j / tubular_subdivisions
            texcoords.append([u, v])

    # Generate faces (as quads, split into two triangles)
    for i in range(radial_subdivisions):
        for j in range(tubular_subdivisions):
            v1 = i * tubular_subdivisions + j
            v2 = (v1 + 1) % tubular_subdivisions + i * tubular_subdivisions
            v3 = ((i + 1) % radial_subdivisions) * tubular_subdivisions + j
            v4 = (v3 + 1) % tubular_subdivisions + ((i + 1) % radial_subdivisions) * tubular_subdivisions

            if clockwise:
                faces.append([v1, v3, v2])  # Triangle 1 (reversed order)
                faces.append([v2, v3, v4])  # Triangle 2 (reversed order)
            else:
                faces.append([v1, v2, v3])  # Triangle 1 (CCW winding)
                faces.append([v2, v4, v3])  # Triangle 2 (CCW winding)

    return np.array(vertices), np.array(normals), np.array(texcoords), np.array(faces)


def generate_tetrahedron(clockwise=False):
    vertices = np.array([
        [1, 1, 1],
        [-1, -1, 1],
        [-1, 1, -1],
        [1, -1, -1]
    ])

    texcoords = np.zeros((4, 2))

    # Faces of the tetrahedron
    faces = np.array([
        [0, 1, 2],
        [0, 3, 1],
        [0, 2, 3],
        [1, 3, 2]
    ])

    # Reverse winding order if clockwise is desired
    if not clockwise:
        faces = faces[:, [0, 2, 1]]

    # Calculate face normals
    normals = []
    for face in faces:
        v0 = vertices[face[0]]
        v1 = vertices[face[1]]
        v2 = vertices[face[2]]
        
        # Calculate two edge vectors
        edge1 = v1 - v0
        edge2 = v2 - v0
        
        # Compute the cross product to get the normal
        normal = np.cross(edge1, edge2)
        
        # Normalize the vector to unit length
        normal = normal / np.linalg.norm(normal)
        
        # Assign this normal to all three vertices of the face (flat shading)
        normals.append(normal)
        normals.append(normal)
        normals.append(normal)

    normals = np.array(normals).reshape(-1, 3)

    return vertices, normals, texcoords, faces


def generate_sphere_grid(radius, subdivisions, spacing):
    positions = generate_grid_positions(spacing)
    vertices, normals, texcoords, faces = generate_sphere_mesh(radius, subdivisions, positions)
    return vertices, normals, texcoords, faces


def generate_sphere_mesh(radius, subdivisions, positions):
    vertices = []
    normals = []
    texcoords = []
    faces = []
    vertex_offset = 0

    for position in positions:
        for i in range(subdivisions + 1):
            lat = np.pi * i / subdivisions
            v = i / subdivisions  # Texture coordinate for latitude
            for j in range(subdivisions + 1):
                lon = 2 * np.pi * j / subdivisions
                u = j / subdivisions  # Texture coordinate for longitude
                x = radius * np.sin(lat) * np.cos(lon) + position[0]
                y = radius * np.sin(lat) * np.sin(lon) + position[1]
                z = radius * np.cos(lat) + position[2]
                vertices.append([x, y, z])
                normals.append([(x - position[0]) / radius, (y - position[1]) / radius, (z - position[2]) / radius])
                texcoords.append([u, 1 - v])  # Flip v to match OpenGL convention

        # Generate faces (as triangles) with correct winding
        for i in range(subdivisions):
            for j in range(subdivisions):
                v1 = vertex_offset + i * (subdivisions + 1) + j
                v2 = v1 + 1
                v3 = vertex_offset + (i + 1) * (subdivisions + 1) + j
                v4 = v3 + 1
                faces.append([v1, v3, v2])  # Triangle 1 (corrected winding)
                faces.append([v2, v3, v4])  # Triangle 2 (corrected winding)

        vertex_offset += (subdivisions + 1) * (subdivisions + 1)

    return vertices, normals, texcoords, faces


def generate_grid_positions(spacing):
    positions = []
    grid_size = 3  # Hardcoded grid size of 3x3x3
    offset = (grid_size - 1) * spacing / 2  # To center the grid around the origin

    for x in range(grid_size):
        for y in range(grid_size):
            for z in range(grid_size):
                # Only include positions on the outer shell of the 3x3x3 grid
                if x in [0, 2] or y in [0, 2] or z in [0, 2]:
                    position = [
                        x * spacing - offset,
                        y * spacing - offset,
                        z * spacing - offset
                    ]
                    positions.append(position)

    return positions


def save_obj(file_path, vertices, normals, texcoords, faces):
    with open(file_path, "w") as f:
        for vertex in vertices:
            f.write(f"v {vertex[0]} {vertex[1]} {vertex[2]}\n")
        for normal in normals:
            f.write(f"vn {normal[0]} {normal[1]} {normal[2]}\n")
        for texcoord in texcoords:
            f.write(f"vt {texcoord[0]} {texcoord[1]}\n")
        for face in faces:
            f.write(f"f {face[0]+1}/{face[0]+1}/{face[0]+1} {face[1]+1}/{face[1]+1}/{face[1]+1} {face[2]+1}/{face[2]+1}/{face[2]+1}\n")


if __name__ == "__main__":
    output_dir = Path("assets/models")
    output_dir.mkdir(parents=True, exist_ok=True)  # Ensure the directory exists

    # Sphere file path
    sphere_path = output_dir.joinpath("sphere.obj")
    if not sphere_path.exists():
        print(f"Generating sphere.obj...")
        radius = 1.0
        subdivisions = 20
        clockwise = False  # Ensure counterclockwise winding
        vertices, normals, texcoords, faces = generate_sphere(radius, subdivisions, clockwise)
        save_obj(sphere_path, vertices, normals, texcoords, faces)
        print(f"Sphere OBJ file saved as {sphere_path}")
    else:
        print(f"Sphere OBJ already exists, skipping generation.")

    # Torus file path
    torus_path = output_dir.joinpath("torus.obj")
    if not torus_path.exists():
        print(f"Generating torus.obj...")
        outer_radius = 1.0
        inner_radius = 0.3
        radial_subdivisions = 30
        tubular_subdivisions = 20
        clockwise = False  # Ensure counterclockwise winding
        vertices, normals, texcoords, faces = generate_torus(outer_radius, inner_radius, radial_subdivisions, tubular_subdivisions, clockwise)
        save_obj(torus_path, vertices, normals, texcoords, faces)
        print(f"Torus OBJ file saved as {torus_path}")
    else:
        print(f"Torus OBJ already exists, skipping generation.")

    # Tetrahedron file path
    tetrahedron_path = output_dir.joinpath("tetrahedron.obj")
    if not tetrahedron_path.exists():
        print(f"Generating tetrahedron.obj...")
        clockwise = False  # Ensure counterclockwise winding
        vertices, normals, texcoords, faces = generate_tetrahedron(clockwise)
        save_obj(tetrahedron_path, vertices, normals, texcoords, faces)
        print(f"Tetrahedron OBJ file saved as {tetrahedron_path}")
    else:
        print(f"Tetrahedron OBJ already exists, skipping generation.")

    # Sphere Grid file path
    sphere_grid_path = output_dir.joinpath("sphere_grid.obj")
    if not sphere_grid_path.exists():
        print(f"Generating sphere_grid.obj...")
        radius = 1.0
        subdivisions = 30
        spacing = radius * 4.0  # Increased spacing between spheres
        vertices, normals, texcoords, faces = generate_sphere_grid(radius, subdivisions, spacing)
        save_obj(sphere_grid_path, vertices, normals, texcoords, faces)
        print(f"Sphere grid OBJ file saved as {sphere_grid_path}")
    else:
        print(f"Sphere grid OBJ already exists, skipping generation.")