#!/usr/bin/env perl
# Génère game/data/instances/zone_feyhin/buildings.bin (format LCBD v1) pour
# l'auberge de Feyhin, à partir des 13 props historiquement posés dans
# scenery.json (indices 310-322). Migration P6 : l'auberge devient une entité
# « Building » éditable, rendue côté client par Engine::LoadBuildings.
#
# Origine du groupe = (88, 0, 100) (point de réapparition « inn »). Chaque prop
# devient une BuildingPart en espace local (offset = position monde - origine).
#
# Reproductible / idempotent : ré-exécuter régénère le même fichier.
# Encodage float32 little-endian (x86) ; entiers little-endian.
#
# Usage : perl tools/asset_pipeline/generate_feyhin_auberge.pl

use strict;
use warnings;

my $ORIGIN_X = 88.0;
my $ORIGIN_Z = 100.0;
my $OUT = "game/data/instances/zone_feyhin/buildings.bin";

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

sub u32 { return pack("V", $_[0]); }
sub u16 { return pack("v", $_[0]); }
sub f32 { return pack("f", $_[0]); }                 # x86 little-endian
sub u64 { return pack("V", $_[0]) . pack("V", 0); }  # guids < 2^32 : low, high
sub str { my $s = shift; return u16(length($s)) . $s; }

my $buf = "";
$buf .= u32(0x4442434C);  # magic "LCBD" (little-endian)
$buf .= u32(1);           # version
$buf .= u32(1);           # buildingCount
$buf .= u32(0);           # reserved

# Building "Auberge".
$buf .= u64(1);                                        # guid
$buf .= str("Auberge");                                # displayName
$buf .= f32($ORIGIN_X) . f32(0.0) . f32($ORIGIN_Z);   # worldPosition
$buf .= f32(0.0);                                      # worldYawDeg
$buf .= f32(1.0);                                      # worldScale
$buf .= u32(scalar @parts);                            # partCount

for my $p (@parts) {
  my ($mesh, $x, $z, $yaw, $scale, $solid, $cr) = @$p;
  $buf .= str($mesh);
  $buf .= f32($x - $ORIGIN_X) . f32(0.0) . f32($z - $ORIGIN_Z); # localPosition
  $buf .= f32(0.0) . f32($yaw) . f32(0.0);                      # localEulerDeg (yaw dominant)
  $buf .= f32($scale);                                          # localScale
  $buf .= pack("C", $solid ? 1 : 0);                            # flags : bit0 = solid
  $buf .= f32($cr);                                             # collisionRadius
}

open(my $fh, ">:raw", $OUT) or die "open $OUT: $!";
print $fh $buf;
close($fh);
printf "wrote %s : %d bytes, %d parts\n", $OUT, length($buf), scalar(@parts);
