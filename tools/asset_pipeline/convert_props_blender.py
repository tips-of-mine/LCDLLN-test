# Conversion FBX -> glTF (.gltf + .bin) des props non encore convertis.
# Reproduit l'export Blender d'origine (matériaux MI_Trim_*, ORM packé, COLOR_0,
# doubleSided, textures trim partagées référencées par URI relatif).
#
# Lancement (PowerShell) :
#   & "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
#       --python tools/asset_pipeline/convert_props_blender.py -- [--only NAME] [--validate]
#
# Args après "--" :
#   --only NAME   : ne traiter qu'un FBX (sans extension), ex. --only Barrel
#   --out DIR     : dossier de sortie (défaut game/data/meshes/props)
#   --validate    : convertit vers un dossier temp (n'écrase pas), pour diff
#
# Effet de bord : écrit des .gltf/.bin dans le dossier de sortie ; logge les échecs.
import bpy, sys, os, glob


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    only = None
    out = None
    validate = False
    i = 0
    while i < len(argv):
        if argv[i] == "--only":
            only = argv[i + 1]
            i += 2
        elif argv[i] == "--out":
            out = argv[i + 1]
            i += 2
        elif argv[i] == "--validate":
            validate = True
            i += 1
        else:
            i += 1
    return only, out, validate


REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
INBOX = os.path.join(REPO, "tools", "asset_pipeline", "inbox", "meshes", "props")
PROPS_OUT = os.path.join(REPO, "game", "data", "meshes", "props")


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def _load_img(fname, non_color):
    # Charge un PNG du dossier props partagé ; règle l'espace colorimétrique.
    path = os.path.join(PROPS_OUT, fname)
    img = bpy.data.images.load(path, check_existing=True)
    if non_color:
        img.colorspace_settings.name = 'Non-Color'
    img.name = os.path.splitext(fname)[0]
    return img


def build_trim_material(trim_type):
    # Reconstruit un matériau trim avec le graphe de nœuds attendu par l'export glTF :
    #   BaseColor (sRGB)            -> Base Color
    #   Normal (Non-Color)         -> Normal Map -> Normal
    #   ORM (Non-Color) G,B        -> Roughness, Metallic  (=> metallicRoughnessTexture)
    # Renvoie None si les 3 textures trim n'existent pas (fallback : matériau importé).
    bc = os.path.join(PROPS_OUT, f"T_Trim_{trim_type}_BaseColor.png")
    nm = os.path.join(PROPS_OUT, f"T_Trim_{trim_type}_Normal.png")
    orm = os.path.join(PROPS_OUT, f"T_Trim_{trim_type}_ORM.png")
    if not (os.path.exists(bc) and os.path.exists(nm) and os.path.exists(orm)):
        return None

    # Libère le nom canonique : renomme un éventuel matériau importé homonyme pour
    # que le matériau reconstruit garde "MI_Trim_<Type>" (sans suffixe .001).
    target = f"MI_Trim_{trim_type}"
    for m in bpy.data.materials:
        if m.name == target:
            m.name = target + "_imported_tmp"

    mat = bpy.data.materials.new(target)
    mat.use_nodes = True
    mat.use_backface_culling = False  # -> doubleSided=true à l'export
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    tbc = nt.nodes.new("ShaderNodeTexImage")
    tbc.image = _load_img(f"T_Trim_{trim_type}_BaseColor.png", False)
    nt.links.new(tbc.outputs["Color"], bsdf.inputs["Base Color"])

    tnm = nt.nodes.new("ShaderNodeTexImage")
    tnm.image = _load_img(f"T_Trim_{trim_type}_Normal.png", True)
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tnm.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])

    torm = nt.nodes.new("ShaderNodeTexImage")
    torm.image = _load_img(f"T_Trim_{trim_type}_ORM.png", True)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(torm.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    return mat


def remap_imported_images():
    # Fallback pour matériaux non-trim : pointe chaque image vers le PNG partagé du
    # dossier props (s'il existe) pour que l'URI exporté soit le nom de fichier nu.
    for img in bpy.data.images:
        base = os.path.basename(img.filepath_raw or img.name)
        cand = os.path.join(PROPS_OUT, base)
        if os.path.exists(cand):
            img.filepath = cand
            img.source = 'FILE'


def assign_trim_materials():
    # Remplace chaque slot matériau MI_Trim_<Type> par un matériau reconstruit.
    cache = {}
    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        for slot in obj.material_slots:
            if slot.material is None:
                continue
            nm = slot.material.name
            if not nm.startswith("MI_Trim_"):
                continue
            ttype = nm[len("MI_Trim_"):].split(".")[0]  # retire suffixe .001
            if ttype not in cache:
                cache[ttype] = build_trim_material(ttype)
            if cache[ttype] is not None:
                slot.material = cache[ttype]


def drop_extra_vertex_colors():
    # Ne garder qu'une couche de couleur de sommet (COLOR_0) ; le reste est inutile
    # (le loader ignore les couleurs). Best-effort, ne doit jamais bloquer la conv.
    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        me = obj.data
        try:
            while len(me.color_attributes) > 1:
                me.color_attributes.remove(me.color_attributes[len(me.color_attributes) - 1])
        except Exception:
            pass


def convert_one(name, out_dir):
    reset_scene()
    src = os.path.join(INBOX, name + ".fbx")
    bpy.ops.import_scene.fbx(filepath=src)
    assign_trim_materials()
    remap_imported_images()
    drop_extra_vertex_colors()
    dst = os.path.join(out_dir, name + ".gltf")
    bpy.ops.export_scene.gltf(
        filepath=dst,
        export_format='GLTF_SEPARATE',   # .gltf + .bin + textures référencées
        export_materials='EXPORT',
        export_keep_originals=True,       # ne pas redupliquer les textures trim
        export_vertex_color='MATERIAL',   # conserve COLOR_0
        export_yup=True,
        use_selection=False,
    )
    return dst


def main():
    only, out, validate = parse_args()
    out_dir = out or (os.path.join(REPO, "tools", "asset_pipeline", "_validate_out")
                      if validate else PROPS_OUT)
    os.makedirs(out_dir, exist_ok=True)
    if only:
        targets = [only]
    else:
        targets = []
        for fbx in sorted(glob.glob(os.path.join(INBOX, "*.fbx"))):
            n = os.path.splitext(os.path.basename(fbx))[0]
            if not os.path.exists(os.path.join(PROPS_OUT, n + ".gltf")):
                targets.append(n)
    ok, failed = [], []
    for n in targets:
        try:
            convert_one(n, out_dir)
            ok.append(n)
            print(f"[convert_props] OK {n}")
        except Exception as e:
            failed.append((n, str(e)))
            print(f"[convert_props] FAIL {n}: {e}")
    print(f"[convert_props] termine : {len(ok)} ok, {len(failed)} echec(s)")
    for n, e in failed:
        print(f"[convert_props]   echec {n}: {e}")


main()
