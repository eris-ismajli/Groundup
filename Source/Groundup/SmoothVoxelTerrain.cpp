#include "SmoothVoxelTerrain.h"

ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
    RootComponent = Mesh;

    // Mesh Settings for Voxel Performance
    Mesh->bUseComplexAsSimpleCollision = true;
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void ASmoothVoxelTerrain::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // SAFETY: Prevent CDO (Template) from running logic that requires a World
    if (HasAnyFlags(RF_ClassDefaultObject) || !GetWorld()) return;

    RebuildTerrain();
}

void ASmoothVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
    RebuildTerrain();
}

void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (!Mesh) return;

    GenerateVoxelData();
    CreateMesh();
}

void ASmoothVoxelTerrain::PrecomputeHeightMap()
{
    int32 MapSide = ChunkSize + 1;
    HeightMap.Empty();
    HeightMap.SetNumZeroed(MapSide * MapSide);

    for (int32 x = 0; x <= ChunkSize; x++)
    {
        for (int32 y = 0; y <= ChunkSize; y++)
        {
            float NoiseValue = FMath::PerlinNoise2D(FVector2D(x + Seed, y + Seed) * NoiseScale);
            // Calculate the absolute float height for this corner
            HeightMap[x * MapSide + y] = (NoiseValue + 1.0f) * HeightMultiplier + (MaxHeight / 4.0f);
        }
    }
}

void ASmoothVoxelTerrain::GenerateVoxelData()
{
    PrecomputeHeightMap();

    VoxelData.Empty();
    VoxelData.SetNumZeroed(ChunkSize * ChunkSize * MaxHeight);

    for (int32 x = 0; x < ChunkSize; x++)
    {
        for (int32 y = 0; y < ChunkSize; y++)
        {
            // Find the ground level at this specific block column center (approx)
            float H = (GetHeightAtCorner(x, y) + GetHeightAtCorner(x + 1, y) + GetHeightAtCorner(x, y + 1) + GetHeightAtCorner(x + 1, y + 1)) / 4.0f;
            int32 GroundLevel = FMath::FloorToInt(H);

            for (int32 z = 0; z < MaxHeight; z++)
            {
                int32 Index = GetIndex(x, y, z);
                if (!VoxelData.IsValidIndex(Index)) continue;

                if (z < GroundLevel - 3) VoxelData[Index] = EVoxelType::Stone;
                else if (z < GroundLevel - 1) VoxelData[Index] = EVoxelType::Dirt;
                else if (z <= GroundLevel) VoxelData[Index] = EVoxelType::Grass;
                else VoxelData[Index] = EVoxelType::Air;
            }
        }
    }
}

FVector ASmoothVoxelTerrain::GetSmoothVertex(int32 x, int32 y, int32 z) const
{
    float TargetH = GetHeightAtCorner(x, y);

    // NEW LOGIC:
    // If the vertex is at the surface layer (the integer Z immediately below or at TargetH), 
    // we move it to TargetH. This "pulls up" the bottom corners to meet the top.
    // If it is above TargetH, it is still clamped down to TargetH.
    // If it is further below, it remains a square cube.
    float FinalZ = (z >= FMath::FloorToInt(TargetH)) ? TargetH : (float)z;

    return FVector(x, y, FinalZ) * CubeSize;
}

// --- Updated CreateMesh with smooth lighting and crack fixes ---
// 1. Corrected CreateMesh: Fixed Top Face vertex order for Clockwise winding
void ASmoothVoxelTerrain::CreateMesh()
{
    if (!Mesh || VoxelData.Num() == 0 || HeightMap.Num() == 0 || IsPendingKillPending()) return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles; // <--- The array is named 'Triangles' here
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    int32 VertexIndex = 0;

    for (int32 x = 0; x < ChunkSize; x++)
    {
        for (int32 y = 0; y < ChunkSize; y++)
        {
            for (int32 z = 0; z < MaxHeight; z++)
            {
                if (GetVoxelAt(x, y, z) == EVoxelType::Air) continue;

                FVector v001 = GetSmoothVertex(x, y, z + 1);
                FVector v011 = GetSmoothVertex(x, y + 1, z + 1);
                FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1);
                FVector v101 = GetSmoothVertex(x + 1, y, z + 1);

                // TOP FACE
                if (GetVoxelAt(x, y, z + 1) == EVoxelType::Air)
                {
                    // CORRECTED: Clockwise winding order (v001 -> v011 -> v111 -> v101)
                    // Looking from top: Bottom-Left -> Top-Left -> Top-Right -> Bottom-Right
                    Vertices.Add(v001);
                    Vertices.Add(v011);
                    Vertices.Add(v111);
                    Vertices.Add(v101);

                    // Normals must match the vertex order
                    Normals.Add(GetSmoothNormal(x, y));          // matches v001
                    Normals.Add(GetSmoothNormal(x, y + 1));      // matches v011
                    Normals.Add(GetSmoothNormal(x + 1, y + 1));  // matches v111
                    Normals.Add(GetSmoothNormal(x + 1, y));      // matches v101

                    for (int i = 0; i < 4; i++) Colors.Add(FLinearColor::White);

                    // UVs must match the vertex order
                    UVs.Add(FVector2D(0, 1)); // v001
                    UVs.Add(FVector2D(0, 0)); // v011
                    UVs.Add(FVector2D(1, 0)); // v111
                    UVs.Add(FVector2D(1, 1)); // v101

                    Triangles.Add(VertexIndex);
                    Triangles.Add(VertexIndex + 1);
                    Triangles.Add(VertexIndex + 2);

                    Triangles.Add(VertexIndex);
                    Triangles.Add(VertexIndex + 2);
                    Triangles.Add(VertexIndex + 3);

                    VertexIndex += 4;
                }

                // Get bottom corners for the rest of the faces
                FVector v000 = GetSmoothVertex(x, y, z);
                FVector v010 = GetSmoothVertex(x, y + 1, z);
                FVector v110 = GetSmoothVertex(x + 1, y + 1, z);
                FVector v100 = GetSmoothVertex(x + 1, y, z);

                // BOTTOM FACE
                if (GetVoxelAt(x, y, z - 1) == EVoxelType::Air)
                    CreateFace(v100, v110, v010, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                // SIDE FACES
                if (GetVoxelAt(x + 1, y, z) == EVoxelType::Air || GetSmoothVertex(x + 1, y, z + 1).Z < v101.Z - 0.1f)
                    CreateFace(v100, v101, v111, v110, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x - 1, y, z) == EVoxelType::Air || GetSmoothVertex(x - 1, y, z + 1).Z < v001.Z - 0.1f)
                    CreateFace(v010, v011, v001, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x, y + 1, z) == EVoxelType::Air || GetSmoothVertex(x, y + 1, z + 1).Z < v111.Z - 0.1f)
                    CreateFace(v110, v111, v011, v010, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x, y - 1, z) == EVoxelType::Air || GetSmoothVertex(x, y - 1, z + 1).Z < v001.Z - 0.1f)
                    CreateFace(v000, v001, v101, v100, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);
            }
        }
    }

    Mesh->ClearAllMeshSections();
    if (Vertices.Num() > 0)
        Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), true);
}

// 2. Corrected CreateFace: Swapped cross-product order to flip normals outward
void ASmoothVoxelTerrain::CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
    TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors)
{
    if (FVector::DistSquared(p1, p3) < 0.001f || FVector::DistSquared(p2, p4) < 0.001f) return;

    // FIX: Swapped (p2-p1) and (p4-p1) to point the normal OUTWARD for CW winding
    FVector Normal = FVector::CrossProduct(p4 - p1, p2 - p1).GetSafeNormal();

    Verts.Add(p1); Verts.Add(p2); Verts.Add(p3); Verts.Add(p4);

    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 1); Tris.Add(VertexIdx + 2);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 2); Tris.Add(VertexIdx + 3);

    for (int i = 0; i < 4; i++) {
        Norms.Add(Normal);
        Colors.Add(FLinearColor::White);
    }

    UVs.Add(FVector2D(0, 1)); UVs.Add(FVector2D(0, 0)); UVs.Add(FVector2D(1, 0)); UVs.Add(FVector2D(1, 1));
    VertexIdx += 4;
}

float ASmoothVoxelTerrain::GetHeightAtCorner(int32 x, int32 y) const
{
    // SAFETY: If HeightMap is empty (during destruction or rebuild), return 0 to prevent crash
    if (HeightMap.Num() == 0) return 0.0f;

    int32 MapSide = ChunkSize + 1;
    x = FMath::Clamp(x, 0, ChunkSize);
    y = FMath::Clamp(y, 0, ChunkSize);

    int32 Index = x * MapSide + y;
    if (!HeightMap.IsValidIndex(Index)) return 0.0f;

    return HeightMap[Index];
}

FVector ASmoothVoxelTerrain::GetSmoothNormal(int32 x, int32 y) const
{
    float hL = GetHeightAtCorner(x - 1, y);
    float hR = GetHeightAtCorner(x + 1, y);
    float hD = GetHeightAtCorner(x, y - 1);
    float hU = GetHeightAtCorner(x, y + 1);

    // This calculates the slope normal. The '2.0f' represents the grid spacing impact.
    return FVector(hL - hR, hD - hU, 2.0f).GetSafeNormal();
}

void ASmoothVoxelTerrain::RemoveVoxel(FVector WorldLocation)
{
    if (!Mesh || HasAnyFlags(RF_ClassDefaultObject)) return;

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

    int32 x = FMath::FloorToInt(LocalPos.X / CubeSize);
    int32 y = FMath::FloorToInt(LocalPos.Y / CubeSize);
    int32 z = FMath::FloorToInt(LocalPos.Z / CubeSize);

    if (x >= 0 && x < ChunkSize && y >= 0 && y < ChunkSize && z >= 0 && z < MaxHeight)
    {
        int32 Index = GetIndex(x, y, z);
        if (VoxelData.IsValidIndex(Index) && VoxelData[Index] != EVoxelType::Air)
        {
            VoxelData[Index] = EVoxelType::Air;
            CreateMesh(); // Fast visual update
        }
    }
}

int32 ASmoothVoxelTerrain::GetIndex(int32 x, int32 y, int32 z) const
{
    return x + (y * ChunkSize) + (z * ChunkSize * ChunkSize);
}

EVoxelType ASmoothVoxelTerrain::GetVoxelAt(int32 x, int32 y, int32 z) const
{
    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight) return EVoxelType::Air;
    return VoxelData[GetIndex(x, y, z)];
}