#include "SmoothVoxelTerrain.h"

ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
    RootComponent = Mesh;

    Mesh->bUseComplexAsSimpleCollision = true;
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
}

// -----------------------------------
// Lifecycle
// -----------------------------------
void ASmoothVoxelTerrain::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (HasAnyFlags(RF_ClassDefaultObject) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;

    RebuildTerrain();
}

void ASmoothVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
    RebuildTerrain();
}

void ASmoothVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Clear mesh data to avoid stale references
    if (Mesh && IsValid(Mesh))
    {
        Mesh->ClearAllMeshSections();
    }
    VoxelData.Empty();
    HeightMap.Empty();

    Super::EndPlay(EndPlayReason);
}

// -----------------------------------
// Terrain Generation
// -----------------------------------
void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (!Mesh || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;

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
            HeightMap[x * MapSide + y] = (NoiseValue + 1.0f) * HeightMultiplier + (MaxHeight / 4.0f);
        }
    }
}

void ASmoothVoxelTerrain::GenerateVoxelData()
{
    PrecomputeHeightMap();

    VoxelData.Empty();
    VoxelData.SetNumZeroed(ChunkSize * ChunkSize * MaxHeight);

    const float MinGrassThickness = 0.2f;

    for (int32 x = 0; x < ChunkSize; x++)
    {
        for (int32 y = 0; y < ChunkSize; y++)
        {
            float h00 = GetHeightAtCorner(x, y);
            float h10 = GetHeightAtCorner(x + 1, y);
            float h01 = GetHeightAtCorner(x, y + 1);
            float h11 = GetHeightAtCorner(x + 1, y + 1);

            float MinCorner = FMath::Min3(h00, h10, FMath::Min(h01, h11));
            int32 GroundLevel = FMath::Clamp(FMath::FloorToInt(MinCorner - MinGrassThickness), 0, MaxHeight - 1);

            for (int32 z = 0; z < MaxHeight; z++)
            {
                int32 Index = GetIndex(x, y, z);
                if (!VoxelData.IsValidIndex(Index)) continue;

                if (z < GroundLevel - 3)
                    VoxelData[Index] = EVoxelType::Stone;
                else if (z < GroundLevel - 1)
                    VoxelData[Index] = EVoxelType::Dirt;
                else if (z == GroundLevel)
                    VoxelData[Index] = EVoxelType::Grass;
                else if (z < GroundLevel)
                    VoxelData[Index] = EVoxelType::Dirt;
                else
                    VoxelData[Index] = EVoxelType::Air;
            }
        }
    }
}

// -----------------------------------
// Mesh Utilities
// -----------------------------------
FVector ASmoothVoxelTerrain::GetSmoothVertex(int32 cornerX, int32 cornerY, int32 cornerZ, int32 vX, int32 vY, int32 vZ) const
{
    if (!bSmoothTerrain)
        return FVector(cornerX, cornerY, cornerZ) * CubeSize;

    float TargetH = GetHeightAtCorner(cornerX, cornerY);
    float FinalZ = (float)cornerZ;

    if (GetVoxelAt(vX, vY, vZ) == EVoxelType::Grass && cornerZ > vZ)
        FinalZ = TargetH;

    return FVector(cornerX, cornerY, FinalZ) * CubeSize;
}

FVector ASmoothVoxelTerrain::GetSmoothNormal(int32 x, int32 y) const
{
    float hL = GetHeightAtCorner(x - 1, y);
    float hR = GetHeightAtCorner(x + 1, y);
    float hD = GetHeightAtCorner(x, y - 1);
    float hU = GetHeightAtCorner(x, y + 1);

    return FVector(hL - hR, hD - hU, 2.0f).GetSafeNormal();
}

float ASmoothVoxelTerrain::GetHeightAtCorner(int32 x, int32 y) const
{
    if (HeightMap.Num() == 0) return 0.0f;

    int32 MapSide = ChunkSize + 1;
    x = FMath::Clamp(x, 0, ChunkSize);
    y = FMath::Clamp(y, 0, ChunkSize);

    int32 Index = x * MapSide + y;
    return HeightMap.IsValidIndex(Index) ? HeightMap[Index] : 0.0f;
}

int32 ASmoothVoxelTerrain::GetIndex(int32 x, int32 y, int32 z) const
{
    return x + (y * ChunkSize) + (z * ChunkSize * ChunkSize);
}

EVoxelType ASmoothVoxelTerrain::GetVoxelAt(int32 x, int32 y, int32 z) const
{
    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight) return EVoxelType::Air;

    int32 Index = GetIndex(x, y, z);
    return VoxelData.IsValidIndex(Index) ? VoxelData[Index] : EVoxelType::Air;
}

// -----------------------------------
// Mesh Generation
// -----------------------------------
void ASmoothVoxelTerrain::CreateMesh()
{
    if (!Mesh || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;

    if (VoxelData.Num() == 0 || HeightMap.Num() == 0)
        return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
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

                // Top face vertices
                FVector v001 = GetSmoothVertex(x, y, z + 1, x, y, z);
                FVector v011 = GetSmoothVertex(x, y + 1, z + 1, x, y, z);
                FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y, z);
                FVector v101 = GetSmoothVertex(x + 1, y, z + 1, x, y, z);

                // Top face
                if (GetVoxelAt(x, y, z + 1) == EVoxelType::Air)
                {
                    Vertices.Add(v001); Vertices.Add(v011); Vertices.Add(v111); Vertices.Add(v101);

                    if (bSmoothTerrain)
                    {
                        Normals.Add(GetSmoothNormal(x, y));
                        Normals.Add(GetSmoothNormal(x, y + 1));
                        Normals.Add(GetSmoothNormal(x + 1, y + 1));
                        Normals.Add(GetSmoothNormal(x + 1, y));
                    }
                    else
                    {
                        for (int i = 0; i < 4; i++) Normals.Add(FVector::UpVector);
                    }

                    for (int i = 0; i < 4; i++) Colors.Add(FLinearColor::White);

                    UVs.Add(FVector2D(0, 1)); UVs.Add(FVector2D(0, 0));
                    UVs.Add(FVector2D(1, 0)); UVs.Add(FVector2D(1, 1));

                    Triangles.Add(VertexIndex); Triangles.Add(VertexIndex + 1); Triangles.Add(VertexIndex + 2);
                    Triangles.Add(VertexIndex); Triangles.Add(VertexIndex + 2); Triangles.Add(VertexIndex + 3);

                    VertexIndex += 4;
                }

                // Bottom vertices
                FVector v000 = GetSmoothVertex(x, y, z, x, y, z);
                FVector v010 = GetSmoothVertex(x, y + 1, z, x, y, z);
                FVector v110 = GetSmoothVertex(x + 1, y + 1, z, x, y, z);
                FVector v100 = GetSmoothVertex(x + 1, y, z, x, y, z);

                // Faces
                if (GetVoxelAt(x, y, z - 1) == EVoxelType::Air)
                    CreateFace(v100, v110, v010, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x + 1, y, z) == EVoxelType::Air || GetSmoothVertex(x + 1, y, z + 1, x + 1, y, z).Z < v101.Z - 0.1f)
                    CreateFace(v100, v101, v111, v110, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x - 1, y, z) == EVoxelType::Air || GetSmoothVertex(x, y, z + 1, x - 1, y, z).Z < v001.Z - 0.1f)
                    CreateFace(v010, v011, v001, v000, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x, y + 1, z) == EVoxelType::Air || GetSmoothVertex(x + 1, y + 1, z + 1, x, y + 1, z).Z < v111.Z - 0.1f)
                    CreateFace(v110, v111, v011, v010, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);

                if (GetVoxelAt(x, y - 1, z) == EVoxelType::Air || GetSmoothVertex(x, y, z + 1, x, y - 1, z).Z < v001.Z - 0.1f)
                    CreateFace(v000, v001, v101, v100, VertexIndex, Vertices, Triangles, Normals, UVs, Colors);
            }
        }
    }

    // Safety: ensure Mesh is still valid before updating
    if (!Mesh || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;

    Mesh->ClearAllMeshSections();
    if (Vertices.Num() > 0)
    {
        Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), true);
    }
}

void ASmoothVoxelTerrain::CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
    TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors)
{
    if (FVector::DistSquared(p1, p3) < 0.001f || FVector::DistSquared(p2, p4) < 0.001f) return;

    FVector Normal = FVector::CrossProduct(p4 - p1, p2 - p1).GetSafeNormal();

    Verts.Add(p1); Verts.Add(p2); Verts.Add(p3); Verts.Add(p4);

    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 1); Tris.Add(VertexIdx + 2);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 2); Tris.Add(VertexIdx + 3);

    for (int i = 0; i < 4; i++)
    {
        Norms.Add(Normal);
        Colors.Add(FLinearColor::White);
    }

    UVs.Add(FVector2D(0, 1)); UVs.Add(FVector2D(0, 0));
    UVs.Add(FVector2D(1, 0)); UVs.Add(FVector2D(1, 1));

    VertexIdx += 4;
}

// -----------------------------------
// Voxel Editing
// -----------------------------------
void ASmoothVoxelTerrain::RemoveVoxel(FVector WorldLocation)
{
    if (!Mesh || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || HasAnyFlags(RF_ClassDefaultObject))
        return;

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

    int32 x = FMath::FloorToInt((LocalPos.X + KINDA_SMALL_NUMBER) / CubeSize);
    int32 y = FMath::FloorToInt((LocalPos.Y + KINDA_SMALL_NUMBER) / CubeSize);
    int32 z = FMath::FloorToInt((LocalPos.Z + KINDA_SMALL_NUMBER) / CubeSize);

    if (z > 0 && GetVoxelAt(x, y, z) == EVoxelType::Air && GetVoxelAt(x, y, z - 1) == EVoxelType::Grass)
    {
        float GrassBaseZ = (z - 1) * CubeSize;
        if (LocalPos.Z > GrassBaseZ)
        {
            z -= 1;
        }
    }

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

    if (VoxelData[Index] == EVoxelType::Air)
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: Voxel is already air – nothing to remove."));
        return;
    }

    VoxelData[Index] = EVoxelType::Air;
    CreateMesh();
}

#if WITH_EDITOR
void ASmoothVoxelTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (!GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || IsTemplate() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        return;

    RebuildTerrain();
}
#endif