#include "SmoothVoxelTerrain.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Components/DynamicMeshComponent.h"

static constexpr float SIDE_FACE_EPSILON = 0.01f;

ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create Dynamic Mesh Component instead of Procedural Mesh
    Mesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
    RootComponent = Mesh;

    // Enable complex collision (like original)
    Mesh->SetComplexAsSimpleCollisionEnabled(true);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
}

ASmoothVoxelTerrain::~ASmoothVoxelTerrain()
{
    bIsDestroyed = true;
    Mesh = nullptr;
}

// -----------------------------------
// Lifecycle
// -----------------------------------
void ASmoothVoxelTerrain::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (bIsDestroyed || HasAnyFlags(RF_ClassDefaultObject) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;
    RebuildTerrain();
}

void ASmoothVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
    if (bIsDestroyed) return;
    RebuildTerrain();
}

void ASmoothVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bIsDestroyed = true;

    // Clear the dynamic mesh
    if (Mesh && IsValid(Mesh))
    {
        Mesh->SetMesh(UE::Geometry::FDynamicMesh3());
    }
    Mesh = nullptr;

    VoxelData.Empty();
    HeightMap.Empty();

    Super::EndPlay(EndPlayReason);
}

// -----------------------------------
// Terrain Generation (unchanged from your original)
// -----------------------------------
void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (bIsDestroyed || !Mesh || !IsValid(Mesh) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
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
// Mesh Utilities (unchanged)
// -----------------------------------
FVector ASmoothVoxelTerrain::GetSmoothVertex(int32 cornerX, int32 cornerY, int32 cornerZ, int32 vX, int32 vY, int32 vZ) const
{
    if (!bSmoothTerrain)
        return FVector(cornerX, cornerY, cornerZ) * CubeSize;

    float TargetH = GetHeightAtCorner(cornerX, cornerY);
    float FinalZ = (float)cornerZ;

    if (GetVoxelAt(vX, vY, vZ) == EVoxelType::Grass && cornerZ > vZ)
    {
        if (GetVoxelAt(vX, vY, vZ + 1) != EVoxelType::Air)
            return FVector(cornerX, cornerY, cornerZ) * CubeSize;
        FinalZ = TargetH;
    }

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

float ASmoothVoxelTerrain::GetInterpolatedHeight(float X, float Y) const
{
    int32 x0 = FMath::FloorToInt(X / CubeSize);
    int32 y0 = FMath::FloorToInt(Y / CubeSize);
    x0 = FMath::Clamp(x0, 0, ChunkSize - 1);
    y0 = FMath::Clamp(y0, 0, ChunkSize - 1);
    int32 x1 = FMath::Min(x0 + 1, ChunkSize);
    int32 y1 = FMath::Min(y0 + 1, ChunkSize);

    float fx = (X - x0 * CubeSize) / CubeSize;
    float fy = (Y - y0 * CubeSize) / CubeSize;

    float h00 = GetHeightAtCorner(x0, y0);
    float h10 = GetHeightAtCorner(x1, y0);
    float h01 = GetHeightAtCorner(x0, y1);
    float h11 = GetHeightAtCorner(x1, y1);

    return FMath::Lerp(FMath::Lerp(h00, h10, fx), FMath::Lerp(h01, h11, fx), fy);
}

float ASmoothVoxelTerrain::GetNeighborTopHeight(int32 neighborX, int32 neighborY, int32 neighborZ, const FVector& point) const
{
    EVoxelType neighborType = GetVoxelAt(neighborX, neighborY, neighborZ);
    if (neighborType == EVoxelType::Air)
        return -FLT_MAX;
    else if (neighborType == EVoxelType::Grass)
    {
        // Direct heightmap value at the corner (point.X, point.Y) – no interpolation
        int32 cornerX = FMath::RoundToInt(point.X / CubeSize);
        int32 cornerY = FMath::RoundToInt(point.Y / CubeSize);
        return GetHeightAtCorner(cornerX, cornerY);
    }
    else // Stone or Dirt
        return (neighborZ + 1) * CubeSize;
}

int32 ASmoothVoxelTerrain::GetIndex(int32 x, int32 y, int32 z) const
{
    return x + (y * ChunkSize) + (z * ChunkSize * ChunkSize);
}

EVoxelType ASmoothVoxelTerrain::GetVoxelAt(int32 x, int32 y, int32 z) const
{
    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight)
        return EVoxelType::Air;
    int32 Index = GetIndex(x, y, z);
    return VoxelData.IsValidIndex(Index) ? VoxelData[Index] : EVoxelType::Air;
}

// -----------------------------------
// Mesh Generation using FDynamicMesh3
// -----------------------------------
void ASmoothVoxelTerrain::CreateMesh()
{
    if (bIsDestroyed || !Mesh || !IsValid(Mesh) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;
    if (VoxelData.Num() == 0 || HeightMap.Num() == 0)
        return;

    TArray<FVector> AllVertices;
    TArray<int32> AllTriangles;
    TArray<int32> VoxelTriCount;
    VoxelTriCount.SetNumZeroed(ChunkSize * ChunkSize * MaxHeight);

    for (int32 x = 0; x < ChunkSize; x++)
        for (int32 y = 0; y < ChunkSize; y++)
            for (int32 z = 0; z < MaxHeight; z++)
            {
                int32 VoxelIdx = GetIndex(x, y, z);
                int32 Before = AllTriangles.Num();
                GenerateVoxelMesh(x, y, z, AllVertices, AllTriangles);
                int32 After = AllTriangles.Num();
                VoxelTriCount[VoxelIdx] = (After - Before) / 3;
            }

    if (AllVertices.Num() == 0) return;

    UE::Geometry::FDynamicMesh3 NewMesh;
    for (const FVector& Pos : AllVertices)
        NewMesh.AppendVertex(FVector3d(Pos.X, Pos.Y, Pos.Z));
    for (int32 i = 0; i < AllTriangles.Num(); i += 3)
        NewMesh.AppendTriangle(AllTriangles[i], AllTriangles[i + 1], AllTriangles[i + 2]);

    // Build triangle map
    VoxelTriangleMap.Empty();
    int32 CurrentTri = 0;
    for (int32 VoxelIdx = 0; VoxelIdx < VoxelTriCount.Num(); VoxelIdx++)
    {
        int32 NumTris = VoxelTriCount[VoxelIdx];
        if (NumTris == 0) continue;
        TArray<int32> TriIDs;
        for (int32 t = 0; t < NumTris; t++)
        {
            int32 TriID = CurrentTri + t;
            NewMesh.SetTriangleGroup(TriID, VoxelIdx);
            TriIDs.Add(TriID);
        }
        VoxelTriangleMap.Add(VoxelIdx, TriIDs);
        CurrentTri += NumTris;
    }

    Mesh->SetMesh(MoveTemp(NewMesh));
    Mesh->NotifyMeshUpdated();
    Mesh->UpdateCollision(true);
}
// Update CreateFace to only fill vertices and triangles
void ASmoothVoxelTerrain::CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
    TArray<FVector>& Verts, TArray<int32>& Tris) const
{
    auto TriangleArea = [](const FVector& a, const FVector& b, const FVector& c) -> float
        {
            return FVector::CrossProduct(b - a, c - a).Size() * 0.5f;
        };

    float area1 = TriangleArea(p1, p2, p3);
    float area2 = TriangleArea(p1, p3, p4);
    if ((area1 + area2) < 0.001f) return;

    Verts.Add(p1); Verts.Add(p2); Verts.Add(p3); Verts.Add(p4);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 1); Tris.Add(VertexIdx + 2);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 2); Tris.Add(VertexIdx + 3);
    VertexIdx += 4;
}

void ASmoothVoxelTerrain::GenerateVoxelMesh(int32 x, int32 y, int32 z,
    TArray<FVector>& OutVertices, TArray<int32>& OutTriangles) const
{
    if (GetVoxelAt(x, y, z) == EVoxelType::Air) return;

    FVector v001 = GetSmoothVertex(x, y, z + 1, x, y, z);
    FVector v011 = GetSmoothVertex(x, y + 1, z + 1, x, y, z);
    FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y, z);
    FVector v101 = GetSmoothVertex(x + 1, y, z + 1, x, y, z);
    FVector v000 = GetSmoothVertex(x, y, z, x, y, z);
    FVector v010 = GetSmoothVertex(x, y + 1, z, x, y, z);
    FVector v110 = GetSmoothVertex(x + 1, y + 1, z, x, y, z);
    FVector v100 = GetSmoothVertex(x + 1, y, z, x, y, z);

    int32 StartVertex = OutVertices.Num();

    // Top face
    if (GetVoxelAt(x, y, z + 1) == EVoxelType::Air)
    {
        OutVertices.Add(v001); OutVertices.Add(v011);
        OutVertices.Add(v111); OutVertices.Add(v101);
        OutTriangles.Add(StartVertex + 0); OutTriangles.Add(StartVertex + 1); OutTriangles.Add(StartVertex + 2);
        OutTriangles.Add(StartVertex + 0); OutTriangles.Add(StartVertex + 2); OutTriangles.Add(StartVertex + 3);
        StartVertex += 4;
    }

    // Bottom face
    if (GetVoxelAt(x, y, z - 1) == EVoxelType::Air)
    {
        CreateFace(v100, v110, v010, v000, StartVertex, OutVertices, OutTriangles);
    }

    // +X face
    if (GetVoxelAt(x + 1, y, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x + 1, y, z, v101) < v101.Z - SIDE_FACE_EPSILON ||
        GetNeighborTopHeight(x + 1, y, z, v111) < v111.Z - SIDE_FACE_EPSILON)
    {
        CreateFace(v100, v101, v111, v110, StartVertex, OutVertices, OutTriangles);
    }

    // -X face
    if (GetVoxelAt(x - 1, y, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x - 1, y, z, v011) < v011.Z - SIDE_FACE_EPSILON ||
        GetNeighborTopHeight(x - 1, y, z, v001) < v001.Z - SIDE_FACE_EPSILON)
    {
        CreateFace(v010, v011, v001, v000, StartVertex, OutVertices, OutTriangles);
    }

    // +Y face
    if (GetVoxelAt(x, y + 1, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x, y + 1, z, v111) < v111.Z - SIDE_FACE_EPSILON ||
        GetNeighborTopHeight(x, y + 1, z, v011) < v011.Z - SIDE_FACE_EPSILON)
    {
        CreateFace(v110, v111, v011, v010, StartVertex, OutVertices, OutTriangles);
    }

    // -Y face
    if (GetVoxelAt(x, y - 1, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x, y - 1, z, v001) < v001.Z - SIDE_FACE_EPSILON ||
        GetNeighborTopHeight(x, y - 1, z, v101) < v101.Z - SIDE_FACE_EPSILON)
    {
        CreateFace(v000, v001, v101, v100, StartVertex, OutVertices, OutTriangles);
    }
}

void ASmoothVoxelTerrain::RegenerateVoxelAndNeighbors(int32 CenterX, int32 CenterY, int32 CenterZ)
{
    if (!Mesh || !IsValid(Mesh)) return;

    UE::Geometry::FDynamicMesh3* DynMesh = Mesh->GetMesh();
    if (!DynMesh) return;

    // 3x3x3 region around center
    TSet<int32> AffectedIndices;
    for (int32 dx = -1; dx <= 1; dx++)
        for (int32 dy = -1; dy <= 1; dy++)
            for (int32 dz = -1; dz <= 1; dz++)
            {
                int32 nx = CenterX + dx, ny = CenterY + dy, nz = CenterZ + dz;
                if (nx >= 0 && nx < ChunkSize && ny >= 0 && ny < ChunkSize && nz >= 0 && nz < MaxHeight)
                    AffectedIndices.Add(GetIndex(nx, ny, nz));
            }

    // Remove old triangles
    for (int32 VoxelIdx : AffectedIndices)
    {
        if (VoxelTriangleMap.Contains(VoxelIdx))
        {
            for (int32 TriID : VoxelTriangleMap[VoxelIdx])
            {
                if (DynMesh->IsTriangle(TriID))
                    DynMesh->RemoveTriangle(TriID, true, true); // remove vertices too
            }
            VoxelTriangleMap.Remove(VoxelIdx);
        }
    }

    // Generate new geometry
    for (int32 VoxelIdx : AffectedIndices)
    {
        int32 x = VoxelIdx % ChunkSize;
        int32 y = (VoxelIdx / ChunkSize) % ChunkSize;
        int32 z = VoxelIdx / (ChunkSize * ChunkSize);

        TArray<FVector> NewVerts;
        TArray<int32> NewTris;
        GenerateVoxelMesh(x, y, z, NewVerts, NewTris);

        if (NewVerts.Num() == 0) continue;

        int32 BaseVertex = DynMesh->MaxVertexID();
        for (const FVector& V : NewVerts)
            DynMesh->AppendVertex(FVector3d(V.X, V.Y, V.Z));

        TArray<int32> NewTriangleIDs;
        for (int32 i = 0; i < NewTris.Num(); i += 3)
        {
            int32 TriID = DynMesh->AppendTriangle(
                BaseVertex + NewTris[i],
                BaseVertex + NewTris[i + 1],
                BaseVertex + NewTris[i + 2]);
            if (TriID >= 0)
            {
                DynMesh->SetTriangleGroup(TriID, VoxelIdx);
                NewTriangleIDs.Add(TriID);
            }
        }
        if (NewTriangleIDs.Num() > 0)
            VoxelTriangleMap.Add(VoxelIdx, NewTriangleIDs);
    }

    Mesh->NotifyMeshUpdated();
    Mesh->UpdateCollision(true);
}
// -----------------------------------
// Voxel Editing (unchanged logic, but now calls CreateMesh which uses dynamic mesh)
// -----------------------------------
void ASmoothVoxelTerrain::RemoveVoxel(FVector WorldLocation)
{
    if (bIsDestroyed || !Mesh || !IsValid(Mesh) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || HasAnyFlags(RF_ClassDefaultObject))
        return;

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

    int32 x = FMath::FloorToInt((LocalPos.X + KINDA_SMALL_NUMBER) / CubeSize);
    int32 y = FMath::FloorToInt((LocalPos.Y + KINDA_SMALL_NUMBER) / CubeSize);
    int32 z = FMath::FloorToInt((LocalPos.Z + KINDA_SMALL_NUMBER) / CubeSize);

    if (z > 0 && GetVoxelAt(x, y, z) == EVoxelType::Air && GetVoxelAt(x, y, z - 1) == EVoxelType::Grass)
    {
        float GrassBaseZ = (z - 1) * CubeSize;
        if (LocalPos.Z > GrassBaseZ)
            z -= 1;
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
    //CreateMesh();   // full rebuild – you can later change to partial update
    RegenerateVoxelAndNeighbors(x, y, z);
}

void ASmoothVoxelTerrain::PlaceVoxel(FVector WorldLocation, EVoxelType Type)
{
    if (bIsDestroyed || !Mesh || !IsValid(Mesh) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || HasAnyFlags(RF_ClassDefaultObject))
        return;

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

    int32 x = FMath::FloorToInt((LocalPos.X + KINDA_SMALL_NUMBER) / CubeSize);
    int32 y = FMath::FloorToInt((LocalPos.Y + KINDA_SMALL_NUMBER) / CubeSize);
    int32 z = FMath::FloorToInt((LocalPos.Z + KINDA_SMALL_NUMBER) / CubeSize);

    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight)
    {
        UE_LOG(LogTemp, Warning, TEXT("PlaceVoxel: coordinates out of bounds (%d, %d, %d)"), x, y, z);
        return;
    }

    int32 Index = GetIndex(x, y, z);
    if (!VoxelData.IsValidIndex(Index))
    {
        UE_LOG(LogTemp, Warning, TEXT("PlaceVoxel: invalid index %d"), Index);
        return;
    }

    if (VoxelData[Index] == Type)
        return;

    VoxelData[Index] = Type;
    //CreateMesh();   // full rebuild – you can later change to partial update
    RegenerateVoxelAndNeighbors(x, y, z);

}