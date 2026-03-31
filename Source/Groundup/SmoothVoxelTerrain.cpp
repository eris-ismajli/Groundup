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
            // Average corner heights to get the approximate ground level at this column
            float H = (GetHeightAtCorner(x, y) + GetHeightAtCorner(x + 1, y) +
                GetHeightAtCorner(x, y + 1) + GetHeightAtCorner(x + 1, y + 1)) / 4.0f;
            int32 GroundLevel = FMath::FloorToInt(H);

            // Fill from bottom up
            for (int32 z = 0; z < MaxHeight; z++)
            {
                int32 Index = GetIndex(x, y, z);
                if (!VoxelData.IsValidIndex(Index)) continue;

                if (z < GroundLevel - 3)
                {
                    VoxelData[Index] = EVoxelType::Stone;
                }
                else if (z < GroundLevel - 1)
                {
                    VoxelData[Index] = EVoxelType::Dirt;
                }
                else if (z == GroundLevel)
                {
                    // Only the topmost solid voxel is Grass
                    VoxelData[Index] = EVoxelType::Grass;
                }
                else if (z < GroundLevel)
                {
                    // Voxels between Dirt depth and just below the surface become Dirt
                    // (they were previously Grass, now Dirt)
                    VoxelData[Index] = EVoxelType::Dirt;
                }
                else
                {
                    VoxelData[Index] = EVoxelType::Air;
                }
            }
        }
    }
}

FVector ASmoothVoxelTerrain::GetSmoothVertex(int32 cornerX, int32 cornerY, int32 cornerZ, int32 vX, int32 vY, int32 vZ) const
{
    if (!bSmoothTerrain)
    {
        return FVector(cornerX, cornerY, cornerZ) * CubeSize;
    }

    float TargetH = GetHeightAtCorner(cornerX, cornerY);
    float FinalZ = (float)cornerZ;

    if (GetVoxelAt(vX, vY, vZ) == EVoxelType::Grass)
    {
        FinalZ = TargetH;
    }
    else if (cornerZ > vZ && GetVoxelAt(vX, vY, vZ + 1) == EVoxelType::Grass)
    {
        FinalZ = TargetH;
    }

    return FVector(cornerX, cornerY, FinalZ) * CubeSize;
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

                // Top Vertices (Context: current voxel x, y, z)
                FVector v001 = GetSmoothVertex(x, y, z + 1, x, y, z);
                FVector v011 = GetSmoothVertex(x, y + 1, z + 1, x, y, z);
                FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y, z);
                FVector v101 = GetSmoothVertex(x + 1, y, z + 1, x, y, z);

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
                    if (bSmoothTerrain)
                    {
                        Normals.Add(GetSmoothNormal(x, y));
                        Normals.Add(GetSmoothNormal(x, y + 1));
                        Normals.Add(GetSmoothNormal(x + 1, y + 1));
                        Normals.Add(GetSmoothNormal(x + 1, y));
                    }
                    else
                    {
                        FVector FlatNormal = FVector::UpVector;
                        for (int i = 0; i < 4; i++) Normals.Add(FlatNormal);
                    }

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
                FVector v000 = GetSmoothVertex(x, y, z, x, y, z);
                FVector v010 = GetSmoothVertex(x, y + 1, z, x, y, z);
                FVector v110 = GetSmoothVertex(x + 1, y + 1, z, x, y, z);
                FVector v100 = GetSmoothVertex(x + 1, y, z, x, y, z);

                // BOTTOM FACE
                if (GetVoxelAt(x, y, z - 1) == EVoxelType::Air)
                    CreateFace(v100, v110, v010, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                // SIDE FACES
            if (GetVoxelAt(x + 1, y, z) == EVoxelType::Air || GetSmoothVertex(x + 1, y, z + 1, x + 1, y, z).Z < v101.Z - 0.1f)
                    CreateFace(v100, v101, v111, v110, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                // Left Face (X-)
                if (GetVoxelAt(x - 1, y, z) == EVoxelType::Air || GetSmoothVertex(x, y, z + 1, x - 1, y, z).Z < v001.Z - 0.1f)
                    CreateFace(v010, v011, v001, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                // Front Face (Y+)
                if (GetVoxelAt(x, y + 1, z) == EVoxelType::Air || GetSmoothVertex(x + 1, y + 1, z + 1, x, y + 1, z).Z < v111.Z - 0.1f)
                    CreateFace(v110, v111, v011, v010, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                // Back Face (Y-)
                if (GetVoxelAt(x, y - 1, z) == EVoxelType::Air || GetSmoothVertex(x, y, z + 1, x, y - 1, z).Z < v001.Z - 0.1f)
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
    // ========================= VALIDATION =========================
    if (!Mesh || HasAnyFlags(RF_ClassDefaultObject))
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: Aborted – Mesh invalid or CDO."));
        return;
    }

    // ========================= COORDINATE CONVERSION =========================
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);
    int32 x = FMath::FloorToInt(LocalPos.X / CubeSize);
    int32 y = FMath::FloorToInt(LocalPos.Y / CubeSize);
    int32 z = FMath::FloorToInt(LocalPos.Z / CubeSize);

    UE_LOG(LogTemp, Warning, TEXT("\n========== REMOVE VOXEL =========="));
    UE_LOG(LogTemp, Warning, TEXT("World click: %s"), *WorldLocation.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Local pos  : %s"), *LocalPos.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Voxel index: x=%d, y=%d, z=%d"), x, y, z);

    // ========================= BOUNDS CHECK =========================
    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight)
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: OUT OF BOUNDS!"));
        return;
    }

    int32 Index = GetIndex(x, y, z);
    if (!VoxelData.IsValidIndex(Index))
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: Invalid index %d"), Index);
        return;
    }

    EVoxelType CurrentType = VoxelData[Index];
    if (CurrentType == EVoxelType::Air)
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: Voxel is already air – nothing to remove."));
        return;
    }

    // ========================= NEIGHBOR INFORMATION =========================
    // Helper lambdas with captures
    auto GetNeighborType = [this, x, y, z](int32 dx, int32 dy, int32 dz) -> EVoxelType
        {
            return GetVoxelAt(x + dx, y + dy, z + dz);
        };

    auto GetCornerHeight = [this](int32 cx, int32 cy) -> float
        {
            return GetHeightAtCorner(cx, cy);
        };

    UE_LOG(LogTemp, Warning, TEXT("\n--- Voxel Details ---"));
    UE_LOG(LogTemp, Warning, TEXT("Current type: %d"), (int32)CurrentType);

    // Corner heights (used for smooth top)
    float h00 = GetCornerHeight(x, y);
    float h10 = GetCornerHeight(x + 1, y);
    float h01 = GetCornerHeight(x, y + 1);
    float h11 = GetCornerHeight(x + 1, y + 1);
    UE_LOG(LogTemp, Warning, TEXT("Corner heights: (%.2f, %.2f, %.2f, %.2f)"), h00, h10, h01, h11);

    // Neighbors types and their top heights (for side condition)
    EVoxelType top = GetNeighborType(0, 0, 1);
    EVoxelType bottom = GetNeighborType(0, 0, -1);
    EVoxelType right = GetNeighborType(1, 0, 0);
    EVoxelType left = GetNeighborType(-1, 0, 0);
    EVoxelType front = GetNeighborType(0, 1, 0);
    EVoxelType back = GetNeighborType(0, -1, 0);

    UE_LOG(LogTemp, Warning, TEXT("Neighbors: Top=%d, Bottom=%d, Right=%d, Left=%d, Front=%d, Back=%d"),
        (int32)top, (int32)bottom, (int32)right, (int32)left, (int32)front, (int32)back);

    // ========================= FACE CONDITIONS (BEFORE REMOVAL) =========================
    bool bFaceTop = (z + 1 >= MaxHeight) || (top == EVoxelType::Air);
    bool bFaceBottom = (z - 1 < 0) || (bottom == EVoxelType::Air);
    bool bFaceRight = (x + 1 >= ChunkSize) || (right == EVoxelType::Air);
    bool bFaceLeft = (x - 1 < 0) || (left == EVoxelType::Air);
    bool bFaceFront = (y + 1 >= ChunkSize) || (front == EVoxelType::Air);
    bool bFaceBack = (y - 1 < 0) || (back == EVoxelType::Air);

    // For side faces, the mesh also adds faces when the neighbor's top is significantly lower.
    // We compute those conditions as well.
    FVector v001 = GetSmoothVertex(x, y, z + 1, x, y, z);
    FVector v011 = GetSmoothVertex(x, y + 1, z + 1, x, y, z);
    FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y, z);
    FVector v101 = GetSmoothVertex(x + 1, y, z + 1, x, y, z);

    bool bRightExtra = false;
    if (!bFaceRight) // neighbor is solid
    {
        FVector nv101 = GetSmoothVertex(x + 1, y, z + 1, x + 1, y, z);
        if (nv101.Z < v101.Z - 0.1f) bRightExtra = true;
    }
    bool bLeftExtra = false;
    if (!bFaceLeft)
    {
        FVector nv001 = GetSmoothVertex(x, y, z + 1, x - 1, y, z);
        if (nv001.Z < v001.Z - 0.1f) bLeftExtra = true;
    }
    bool bFrontExtra = false;
    if (!bFaceFront)
    {
        FVector nv111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y + 1, z);
        if (nv111.Z < v111.Z - 0.1f) bFrontExtra = true;
    }
    bool bBackExtra = false;
    if (!bFaceBack)
    {
        FVector nv001 = GetSmoothVertex(x, y, z + 1, x, y - 1, z);
        if (nv001.Z < v001.Z - 0.1f) bBackExtra = true;
    }

    // Build list of faces that would be generated (including extra conditions)
    TArray<FString> GeneratedFaces;
    if (bFaceTop) GeneratedFaces.Add(TEXT("Top"));
    if (bFaceBottom) GeneratedFaces.Add(TEXT("Bottom"));
    if (bFaceRight || bRightExtra) GeneratedFaces.Add(TEXT("Right"));
    if (bFaceLeft || bLeftExtra) GeneratedFaces.Add(TEXT("Left"));
    if (bFaceFront || bFrontExtra) GeneratedFaces.Add(TEXT("Front"));
    if (bFaceBack || bBackExtra) GeneratedFaces.Add(TEXT("Back"));

    // ========================= LOG THE FACES =========================
    if (GeneratedFaces.Num() == 6)
    {
        UE_LOG(LogTemp, Warning, TEXT("Faces that will be removed (before removal): ALL 6 faces"));
    }
    else if (GeneratedFaces.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Faces that will be removed (before removal): NONE (voxel is completely buried)"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Faces that will be removed (before removal): %s"), *FString::Join(GeneratedFaces, TEXT(", ")));
    }

    // Log extra reasons
    if (!bFaceRight && bRightExtra)
        UE_LOG(LogTemp, Warning, TEXT("  Right face added due to height difference (neighbor top lower)"));
    if (!bFaceLeft && bLeftExtra)
        UE_LOG(LogTemp, Warning, TEXT("  Left face added due to height difference"));
    if (!bFaceFront && bFrontExtra)
        UE_LOG(LogTemp, Warning, TEXT("  Front face added due to height difference"));
    if (!bFaceBack && bBackExtra)
        UE_LOG(LogTemp, Warning, TEXT("  Back face added due to height difference"));

    // ========================= PERFORM REMOVAL =========================
    UE_LOG(LogTemp, Warning, TEXT("\nRemoving voxel and rebuilding mesh..."));
    VoxelData[Index] = EVoxelType::Air;
    CreateMesh();
    UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel completed.\n=================================================\n"));
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

#if WITH_EDITOR
void ASmoothVoxelTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RebuildTerrain();
}
#endif