// vdb_remesh.cpp
// Usage:
//   vdb_remesh --in input.obj --out output.obj [--voxel-size 0.05] [--isovalue 0.0] [--adaptivity 0.0]
//
// --voxel-size   : size of each voxel (default 0.05). Smaller = more detail, heavier.
// --isovalue     : volume offset; negative = expand, positive = shrink (default 0.0).
// --adaptivity   : 0.0 = uniform quads, 1.0 = max triangle reduction (default 0.0).

#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal OBJ I/O
// ---------------------------------------------------------------------------

struct ObjMesh {
    std::vector<std::array<float, 3>> verts;
    std::vector<std::array<int, 3>>   tris;   // triangles  (0-based indices)
    std::vector<std::array<int, 4>>   quads;  // quads      (0-based indices)
};

static bool load_obj(const std::string &path, ObjMesh &out)
{
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Error: cannot open " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            out.verts.push_back({x, y, z});
        }
        else if (token == "f") {
            // Support "f v", "f v/vt", "f v/vt/vn", "f v//vn"
            std::vector<int> idx;
            std::string elem;
            while (ss >> elem) {
                int v = std::stoi(elem);   // takes chars up to '/'
                if (v < 0)
                    v = (int)out.verts.size() + v;  // relative
                else
                    v -= 1;                          // 1-based -> 0-based
                idx.push_back(v);
            }
            if (idx.size() == 3) {
                out.tris.push_back({idx[0], idx[1], idx[2]});
            } else if (idx.size() == 4) {
                out.quads.push_back({idx[0], idx[1], idx[2], idx[3]});
            } else if (idx.size() > 4) {
                // fan-triangulate
                for (int i = 1; i + 1 < (int)idx.size(); ++i)
                    out.tris.push_back({idx[0], idx[i], idx[i + 1]});
            }
        }
    }
    return !out.verts.empty();
}

static bool save_obj(const std::string &path, const ObjMesh &mesh)
{
    std::ofstream f(path);
    if (!f) {
        std::cerr << "Error: cannot write " << path << "\n";
        return false;
    }

    f << "# vdb_remesh output\n";
    for (auto &v : mesh.verts)
        f << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";

    for (auto &q : mesh.quads)
        f << "f " << q[0]+1 << " " << q[1]+1 << " " << q[2]+1 << " " << q[3]+1 << "\n";

    for (auto &t : mesh.tris)
        f << "f " << t[0]+1 << " " << t[1]+1 << " " << t[2]+1 << "\n";

    return true;
}

// ---------------------------------------------------------------------------
// Remesh
// ---------------------------------------------------------------------------

static ObjMesh remesh(const ObjMesh &in,
                      float voxel_size,
                      float isovalue,
                      float adaptivity)
{
    // Build OpenVDB point / triangle lists
    // Quads in input are split to triangles for meshToLevelSet
    std::vector<openvdb::Vec3s> points;
    points.reserve(in.verts.size());
    for (auto &v : in.verts)
        points.emplace_back(v[0], v[1], v[2]);

    std::vector<openvdb::Vec3I> triangles;
    triangles.reserve(in.tris.size() + in.quads.size() * 2);
    for (auto &t : in.tris)
        triangles.emplace_back(t[0], t[1], t[2]);
    for (auto &q : in.quads) {
        triangles.emplace_back(q[0], q[1], q[2]);
        triangles.emplace_back(q[0], q[2], q[3]);
    }

    // Build level set  (mirrors Blender's remesh_voxel_level_set_create)
    auto transform = openvdb::math::Transform::createLinearTransform(voxel_size);
    openvdb::FloatGrid::Ptr grid =
        openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(
            *transform, points, triangles, 1.0f);

    // Volume -> mesh  (mirrors Blender's remesh_voxel_volume_to_mesh)
    std::vector<openvdb::Vec3s> out_verts;
    std::vector<openvdb::Vec4I> out_quads;
    std::vector<openvdb::Vec3I> out_tris;
    openvdb::tools::volumeToMesh<openvdb::FloatGrid>(
        *grid, out_verts, out_tris, out_quads,
        isovalue, adaptivity,
        /*relaxDisorientedTriangles=*/false);

    // Convert back to ObjMesh
    ObjMesh result;
    result.verts.reserve(out_verts.size());
    for (auto &v : out_verts)
        result.verts.push_back({v.x(), v.y(), v.z()});

    // NOTE: Blender reverses winding for quads (q[0],q[3],q[2],q[1])
    result.quads.reserve(out_quads.size());
    for (auto &q : out_quads)
        result.quads.push_back({(int)q[0], (int)q[3], (int)q[2], (int)q[1]});

    result.tris.reserve(out_tris.size());
    for (auto &t : out_tris)
        result.tris.push_back({(int)t[2], (int)t[1], (int)t[0]});

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void print_usage()
{
    std::cout <<
        "Usage: vdb_remesh --in <input.obj> --out <output.obj>\n"
        "                  [--voxel-size <float>]   default: 0.05\n"
        "                  [--isovalue   <float>]   default: 0.0\n"
        "                  [--adaptivity <float>]   default: 0.0  (range 0-1)\n"
        "\n"
        "  --voxel-size   Voxel size. Smaller = more detail, heavier computation.\n"
        "  --isovalue     Equivalent to Blender's 'Volume': negative expands the\n"
        "                 mesh, positive shrinks it.\n"
        "  --adaptivity   0 = uniform quads; 1 = max triangle reduction.\n";
}

int main(int argc, char **argv)
{
    std::string in_path, out_path;
    float voxel_size = 0.05f;
    float isovalue   = 0.0f;
    float adaptivity = 0.0f;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--in" || a == "-i") && i + 1 < argc) {
            in_path = argv[++i];
        } else if ((a == "--out" || a == "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--voxel-size" && i + 1 < argc) {
            voxel_size = std::stof(argv[++i]);
        } else if (a == "--isovalue" && i + 1 < argc) {
            isovalue = std::stof(argv[++i]);
        } else if (a == "--adaptivity" && i + 1 < argc) {
            adaptivity = std::stof(argv[++i]);
        } else if (a == "--help" || a == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            print_usage();
            return 1;
        }
    }

    if (in_path.empty() || out_path.empty()) {
        std::cerr << "Error: --in and --out are required.\n\n";
        print_usage();
        return 1;
    }

    if (voxel_size <= 0.0f) {
        std::cerr << "Error: --voxel-size must be > 0\n";
        return 1;
    }
    adaptivity = std::max(0.0f, std::min(1.0f, adaptivity));

    openvdb::initialize();

    std::cout << "Loading " << in_path << " ...\n";
    ObjMesh input;
    if (!load_obj(in_path, input)) return 1;
    std::cout << "  " << input.verts.size() << " verts, "
              << input.tris.size()  << " tris, "
              << input.quads.size() << " quads\n";

    std::cout << "Remeshing (voxel_size=" << voxel_size
              << " isovalue=" << isovalue
              << " adaptivity=" << adaptivity << ") ...\n";

    ObjMesh output = remesh(input, voxel_size, isovalue, adaptivity);

    if (output.verts.empty()) {
        std::cerr << "Error: remesh produced empty mesh. "
                     "Try a smaller --voxel-size or adjust --isovalue.\n";
        return 1;
    }

    std::cout << "  " << output.verts.size() << " verts, "
              << output.tris.size()  << " tris, "
              << output.quads.size() << " quads\n";

    std::cout << "Writing " << out_path << " ...\n";
    if (!save_obj(out_path, output)) return 1;

    std::cout << "Done.\n";
    return 0;
}
