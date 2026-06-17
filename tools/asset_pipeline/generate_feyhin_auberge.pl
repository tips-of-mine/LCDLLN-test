#!/usr/bin/env perl
# Migration de l'auberge de Feyhin vers le système Building (références +
# bibliothèque). Génère DEUX fichiers à partir des 13 props historiquement
# posés dans scenery.json (indices 310-322) :
#
#   1. game/data/buildings/templates/tavern.json
#      => bibliothèque : type "tavern", variante "auberge_terrasse" (13 pièces
#         en espace local, origine = (88,100)).
#   2. game/data/instances/zone_feyhin/buildings.bin (LCBD v1)
#      => la carte : UNE référence (type=tavern, variante=auberge_terrasse) à
#         l'origine monde (88, 0, 100).
#
# Le jeu (Engine::LoadBuildings) lit buildings.bin, résout la référence contre
# la bibliothèque, et affiche les pièces. Reproductible / idempotent.
#
# Usage : perl tools/asset_pipeline/generate_feyhin_auberge.pl

use strict;
use warnings;

my $ORIGIN_X = 88.0;
my $ORIGIN_Z = 100.0;
my $TYPE     = "tavern";
my $VARIANT  = "auberge_terrasse";

# [mesh, x, z, yaw_deg, scale, solid(1/0), collision_radius]
my @parts = (
  ["meshes/props/Stall_Cart_Empty.gltf", 88.0, 100.0, 180.0, 1.0, 1, 1.0],
  ["meshes/props/Table_Large.gltf",      90.5, 100.6,   0.0, 1.0, 1, 0.9],
  ["meshes/props/Stool.gltf",            89.6, 100.6,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Stool.gltf",            91.4, 100.6,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Table_Large.gltf",      90.5,  98.6,   0.0, 1.0, 1, 0.9],
  ["meshes/props/Stool.gltf",            89.6,  98.6,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Stool.gltf",            91.4,  98.6,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Bench.gltf",            88.0, 102.2,   0.0, 1.0, 1, 0.5],
  ["meshes/props/Barrel.gltf",           86.3, 101.2,   0.0, 1.0, 1, 0.4],
  ["meshes/props/Barrel_Apples.gltf",    86.3, 100.0,   0.0, 1.0, 1, 0.4],
  ["meshes/props/Banner_1.gltf",         88.0,  97.8,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Torch_Metal.gltf",      86.2,  98.4,   0.0, 1.0, 0, 0.0],
  ["meshes/props/Torch_Metal.gltf",      90.4, 102.2,   0.0, 1.0, 0, 0.0],
);

# --- 1. Bibliothèque : tavern.json ---------------------------------------
my $tpl_dir = "game/data/buildings/templates";
mkdir "game/data/buildings" unless -d "game/data/buildings";
mkdir $tpl_dir unless -d $tpl_dir;

my @plines;
for my $i (0 .. $#parts) {
  my ($mesh,$x,$z,$yaw,$scale,$solid,$cr) = @{$parts[$i]};
  my $lx = sprintf("%g", $x - $ORIGIN_X);  # %g : pas d'artefact flottant
  my $lz = sprintf("%g", $z - $ORIGIN_Z);
  my $sol = $solid ? "true" : "false";
  push @plines, sprintf(
    '        "%d": { "mesh": "%s", "x": %s, "y": 0, "z": %s, "rx": 0, "ry": %g, "rz": 0, "scale": %g, "solid": %s, "collision_radius": %g }%s',
    $i, $mesh, $lx, $lz, $yaw, $scale, $sol, $cr, ($i == $#parts ? "" : ","));
}
my $nparts = scalar @parts;
open(my $jf, ">:raw", "$tpl_dir/tavern.json") or die $!;
print $jf "{\n";
print $jf "  \"type\": \"$TYPE\",\n";
print $jf "  \"displayName\": \"Taverne / Auberge\",\n";
print $jf "  \"variants\": {\n";
print $jf "    \"count\": 1,\n";
print $jf "    \"0\": {\n";
print $jf "      \"id\": \"$VARIANT\",\n";
print $jf "      \"displayName\": \"Auberge - terrasse de Feyhin\",\n";
print $jf "      \"parts\": {\n";
print $jf "        \"count\": $nparts,\n";
print $jf join("\n", @plines), "\n";
print $jf "      }\n";
print $jf "    }\n";
print $jf "  }\n";
print $jf "}\n";
close($jf);
print "wrote $tpl_dir/tavern.json ($nparts parts)\n";

# --- 2. Carte : buildings.bin (une référence) ----------------------------
sub u32 { return pack("V", $_[0]); }
sub u16 { return pack("v", $_[0]); }
sub f32 { return pack("f", $_[0]); }                 # x86 little-endian
sub u64 { return pack("V", $_[0]) . pack("V", 0); }  # guids < 2^32
sub str { my $s = shift; return u16(length($s)) . $s; }

my $buf = "";
$buf .= u32(0x4442434C);  # magic "LCBD"
$buf .= u32(1);           # version
$buf .= u32(1);           # placementCount
$buf .= u32(0);           # reserved
# Placement : référence vers tavern/auberge_terrasse à (88,0,100).
$buf .= u64(1);                                       # guid
$buf .= str($TYPE);                                   # templateType
$buf .= str($VARIANT);                                # variantId
$buf .= str("Auberge");                               # displayName
$buf .= f32($ORIGIN_X) . f32(0.0) . f32($ORIGIN_Z);  # worldPosition
$buf .= f32(0.0);                                     # worldYawDeg
$buf .= f32(1.0);                                     # worldScale

my $inst_dir = "game/data/instances/zone_feyhin";
mkdir "game/data/instances" unless -d "game/data/instances";
mkdir $inst_dir unless -d $inst_dir;
open(my $bf, ">:raw", "$inst_dir/buildings.bin") or die $!;
print $bf $buf;
close($bf);
printf "wrote %s/buildings.bin : %d bytes (1 placement)\n", $inst_dir, length($buf);
