#include "SmoothVoxelTerrain.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"

using namespace UE::Geometry;

// -------------------------------------------------------------------
// Lifetime & Basics
// -------------------------------------------------------------------
ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = false;

    MeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
    RootComponent = MeshComponent;

    MeshComponent->SetVisibility(true);
    MeshComponent->SetHiddenInGame(false);
    MeshComponent->SetCastShadow(true);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

ASmoothVoxelTerrain::~ASmoothVoxelTerrain()
{
    bIsDestroyed = true;
    MeshComponent = nullptr;
}

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
    if (MeshComponent && IsValid(MeshComponent))
    {
        if (UDynamicMesh* Mesh = MeshComponent->GetDynamicMesh())
            Mesh->Reset();
    }
    MeshComponent = nullptr;
    VoxelData.Empty();
    HeightMap.Empty();
    VoxelTriangles.Empty();
    Super::EndPlay(EndPlayReason);
}

// -------------------------------------------------------------------
// Terrain Generation (unchanged logic)
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Geometry Helpers (unchanged from your working version)
// -------------------------------------------------------------------
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
        return GetInterpolatedHeight(point.X, point.Y);
    else
        return (neighborZ + 1) * CubeSize;
}

int32 ASmoothVoxelTerrain::GetIndex(int32 x, int32 y, int32 z) const
{
    return x + (y * ChunkSize) + (z * ChunkSize * ChunkSize);
}

void ASmoothVoxelTerrain::IndexToXYZ(int32 Idx, int32& OutX, int32& OutY, int32& OutZ) const
{
    OutZ = Idx / (ChunkSize * ChunkSize);
    int32 Remaining = Idx % (ChunkSize * ChunkSize);
    OutY = Remaining / ChunkSize;
    OutX = Remaining % ChunkSize;
}

EVoxelType ASmoothVoxelTerrain::GetVoxelAt(int32 x, int32 y, int32 z) const
{
    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight)
        return EVoxelType::Air;
    int32 Index = GetIndex(x, y, z);
    return VoxelData.IsValidIndex(Index) ? VoxelData[Index] : EVoxelType::Air;
}

// -------------------------------------------------------------------
// Core Mesh Building – Full Build (initial)
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::CreateMesh()
{
    if (bIsDestroyed || !MeshComponent || !IsValid(MeshComponent) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;
    if (VoxelData.Num() == 0 || HeightMap.Num() == 0)
        return;

    // Reset triangle mapping
    VoxelTriangles.SetNum(VoxelData.Num());
    for (auto& Arr : VoxelTriangles)
        Arr.Empty();

    UDynamicMesh* NewMesh = NewObject<UDynamicMesh>(this);
    NewMesh->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            MeshOut.Clear();
            MeshOut.EnableAttributes();
            FDynamicMeshAttributeSet* Attr = MeshOut.Attributes();
            if (!Attr) return;
            Attr->SetNumUVLayers(1);
            FDynamicMeshUVOverlay* UVOverlay = Attr->GetUVLayer(0);
            FDynamicMeshNormalOverlay* NormalOverlay = Attr->PrimaryNormals();
            if (!UVOverlay || !NormalOverlay) return;

            // Build all voxels
            for (int32 x = 0; x < ChunkSize; x++)
            {
                for (int32 y = 0; y < ChunkSize; y++)
                {
                    for (int32 z = 0; z < MaxHeight; z++)
                    {
                        if (GetVoxelAt(x, y, z) == EVoxelType::Air) continue;
                        int32 VoxelIdx = GetIndex(x, y, z);
                        AppendVoxelFaces(x, y, z, MeshOut, VoxelTriangles[VoxelIdx]);
                    }
                }
            }
        });

    MeshComponent->SetDynamicMesh(NewMesh);
    MeshComponent->UpdateCollision(true);
    if (TerrainMaterial) MeshComponent->SetMaterial(0, TerrainMaterial);
    MeshComponent->SetVisibility(true);
    bMeshBuilt = true;
}

// -------------------------------------------------------------------
// Incremental Update – Rebuild a Voxel and its Face‑Neighbors
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::UpdateVoxelRegion(int32 CenterX, int32 CenterY, int32 CenterZ)
{
    if (!MeshComponent || !MeshComponent->GetDynamicMesh())
        return;

    // Collect affected voxel indices (center + 6 face neighbors)
    TSet<int32> AffectedIndices;
    AffectedIndices.Add(GetIndex(CenterX, CenterY, CenterZ));

    const FIntVector Neighbors[6] = {
        FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
        FIntVector(0, 1, 0), FIntVector(0,-1, 0),
        FIntVector(0, 0, 1), FIntVector(0, 0,-1)
    };
    for (const auto& Off : Neighbors)
    {
        int32 nx = CenterX + Off.X, ny = CenterY + Off.Y, nz = CenterZ + Off.Z;
        if (nx >= 0 && nx < ChunkSize && ny >= 0 && ny < ChunkSize && nz >= 0 && nz < MaxHeight)
            AffectedIndices.Add(GetIndex(nx, ny, nz));
    }

    // Edit the existing dynamic mesh
    MeshComponent->GetDynamicMesh()->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            // 1. Remove old triangles of affected voxels
            for (int32 Idx : AffectedIndices)
            {
                for (int32 TriID : VoxelTriangles[Idx])
                {
                    if (MeshOut.IsTriangle(TriID))
                        MeshOut.RemoveTriangle(TriID, true, false); // remove, keep orphaned vertices
                }
                VoxelTriangles[Idx].Empty();
            }

            // 2. Regenerate faces for affected voxels (only if not air)
            for (int32 Idx : AffectedIndices)
            {
                int32 x, y, z;
                IndexToXYZ(Idx, x, y, z);
                if (GetVoxelAt(x, y, z) != EVoxelType::Air)
                {
                    AppendVoxelFaces(x, y, z, MeshOut, VoxelTriangles[Idx]);
                }
            }
        });

    // Update collision and notify
    //MeshComponent->NotifyMeshUpdated();
    //MeshComponent->UpdateCollision(true);
}

// -------------------------------------------------------------------
// Append a Single Voxel’s Faces to a Dynamic Mesh
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::AppendVoxelFaces(int32 x, int32 y, int32 z, FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs)
{
    // Cache attribute overlays
    FDynamicMeshAttributeSet* Attr = Mesh.Attributes();
    if (!Attr) return;
    FDynamicMeshUVOverlay* UVOverlay = Attr->GetUVLayer(0);
    FDynamicMeshNormalOverlay* NormalOverlay = Attr->PrimaryNormals();
    if (!UVOverlay || !NormalOverlay) return;

    // Precompute the 8 corners (same as before)
    FVector v000 = GetSmoothVertex(x, y, z, x, y, z);
    FVector v100 = GetSmoothVertex(x + 1, y, z, x, y, z);
    FVector v010 = GetSmoothVertex(x, y + 1, z, x, y, z);
    FVector v110 = GetSmoothVertex(x + 1, y + 1, z, x, y, z);
    FVector v001 = GetSmoothVertex(x, y, z + 1, x, y, z);
    FVector v101 = GetSmoothVertex(x + 1, y, z + 1, x, y, z);
    FVector v011 = GetSmoothVertex(x, y + 1, z + 1, x, y, z);
    FVector v111 = GetSmoothVertex(x + 1, y + 1, z + 1, x, y, z);

    // Lambda to add a quad (two triangles) with normals and UVs
    auto AddQuad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D,
        const FVector& NormalA, const FVector& NormalB, const FVector& NormalC, const FVector& NormalD,
        const FVector2D& UV_A, const FVector2D& UV_B, const FVector2D& UV_C, const FVector2D& UV_D)
        {
            int32 vA = Mesh.AppendVertex(FVector3d(A));
            int32 vB = Mesh.AppendVertex(FVector3d(B));
            int32 vC = Mesh.AppendVertex(FVector3d(C));
            int32 vD = Mesh.AppendVertex(FVector3d(D));

            // Triangle 1: A-B-C
            int32 t1 = Mesh.AppendTriangle(vA, vB, vC);
            if (t1 != FDynamicMesh3::InvalidID)
            {
                OutTriIDs.Add(t1);
                // Set normal overlay elements
                int32 nA = NormalOverlay->AppendElement(FVector3f(NormalA));
                int32 nB = NormalOverlay->AppendElement(FVector3f(NormalB));
                int32 nC = NormalOverlay->AppendElement(FVector3f(NormalC));
                NormalOverlay->SetTriangle(t1, FIndex3i(nA, nB, nC));
                // Set UV overlay
                int32 uvA = UVOverlay->AppendElement(FVector2f(UV_A));
                int32 uvB = UVOverlay->AppendElement(FVector2f(UV_B));
                int32 uvC = UVOverlay->AppendElement(FVector2f(UV_C));
                UVOverlay->SetTriangle(t1, FIndex3i(uvA, uvB, uvC));
            }

            // Triangle 2: A-C-D
            int32 t2 = Mesh.AppendTriangle(vA, vC, vD);
            if (t2 != FDynamicMesh3::InvalidID)
            {
                OutTriIDs.Add(t2);
                int32 nA = NormalOverlay->AppendElement(FVector3f(NormalA));
                int32 nC = NormalOverlay->AppendElement(FVector3f(NormalC));
                int32 nD = NormalOverlay->AppendElement(FVector3f(NormalD));
                NormalOverlay->SetTriangle(t2, FIndex3i(nA, nC, nD));
                int32 uvA = UVOverlay->AppendElement(FVector2f(UV_A));
                int32 uvC = UVOverlay->AppendElement(FVector2f(UV_C));
                int32 uvD = UVOverlay->AppendElement(FVector2f(UV_D));
                UVOverlay->SetTriangle(t2, FIndex3i(uvA, uvC, uvD));
            }
        };

    // Helper for faces where all four normals are the same
    auto AddQuadFlat = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal)
        {
            AddQuad(A, B, C, D, Normal, Normal, Normal, Normal,
                FVector2D(0, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1));
        };

    // Top face (+Z)
    if (GetVoxelAt(x, y, z + 1) == EVoxelType::Air)
    {
        if (bSmoothTerrain)
        {
            AddQuad(v001, v011, v111, v101,
                GetSmoothNormal(x, y), GetSmoothNormal(x, y + 1),
                GetSmoothNormal(x + 1, y + 1), GetSmoothNormal(x + 1, y),
                FVector2D(0, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1));
        }
        else
        {
            AddQuadFlat(v001, v011, v111, v101, FVector::UpVector);
        }
    }

    // Bottom face (-Z)
    if (GetVoxelAt(x, y, z - 1) == EVoxelType::Air)
        AddQuadFlat(v100, v110, v010, v000, FVector::DownVector);

    // +X face
    if (GetVoxelAt(x + 1, y, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x + 1, y, z, v100) < v100.Z ||
        GetNeighborTopHeight(x + 1, y, z, v101) < v101.Z ||
        GetNeighborTopHeight(x + 1, y, z, v111) < v111.Z ||
        GetNeighborTopHeight(x + 1, y, z, v110) < v110.Z)
    {
        AddQuadFlat(v100, v101, v111, v110, FVector::RightVector);
    }

    // -X face
    if (GetVoxelAt(x - 1, y, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x - 1, y, z, v010) < v010.Z ||
        GetNeighborTopHeight(x - 1, y, z, v011) < v011.Z ||
        GetNeighborTopHeight(x - 1, y, z, v001) < v001.Z ||
        GetNeighborTopHeight(x - 1, y, z, v000) < v000.Z)
    {
        AddQuadFlat(v010, v011, v001, v000, FVector::LeftVector);
    }

    // +Y face
    if (GetVoxelAt(x, y + 1, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x, y + 1, z, v110) < v110.Z ||
        GetNeighborTopHeight(x, y + 1, z, v111) < v111.Z ||
        GetNeighborTopHeight(x, y + 1, z, v011) < v011.Z ||
        GetNeighborTopHeight(x, y + 1, z, v010) < v010.Z)
    {
        AddQuadFlat(v110, v111, v011, v010, FVector::ForwardVector);
    }

    // -Y face
    if (GetVoxelAt(x, y - 1, z) == EVoxelType::Air ||
        GetNeighborTopHeight(x, y - 1, z, v000) < v000.Z ||
        GetNeighborTopHeight(x, y - 1, z, v001) < v001.Z ||
        GetNeighborTopHeight(x, y - 1, z, v101) < v101.Z ||
        GetNeighborTopHeight(x, y - 1, z, v100) < v100.Z)
    {
        AddQuadFlat(v000, v001, v101, v100, FVector::BackwardVector);
    }
}

// -------------------------------------------------------------------
// Legacy CreateFace (kept for compatibility, not used)
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
    TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms, TArray<FVector2D>& UVs)
{
    auto TriangleArea = [](const FVector& a, const FVector& b, const FVector& c) -> float
        {
            return FVector::CrossProduct(b - a, c - a).Size() * 0.5f;
        };
    float area1 = TriangleArea(p1, p2, p3);
    float area2 = TriangleArea(p1, p3, p4);
    if ((area1 + area2) < 0.001f) return;

    FVector Normal = FVector::CrossProduct(p4 - p1, p2 - p1).GetSafeNormal();
    Verts.Add(p1); Verts.Add(p2); Verts.Add(p3); Verts.Add(p4);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 1); Tris.Add(VertexIdx + 2);
    Tris.Add(VertexIdx); Tris.Add(VertexIdx + 2); Tris.Add(VertexIdx + 3);
    for (int i = 0; i < 4; i++) Norms.Add(Normal);
    UVs.Add(FVector2D(0, 1)); UVs.Add(FVector2D(0, 0));
    UVs.Add(FVector2D(1, 0)); UVs.Add(FVector2D(1, 1));
    VertexIdx += 4;
}

// -------------------------------------------------------------------
// Voxel Editing – Now using Incremental Update
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::RemoveVoxel(FVector WorldLocation)
{
    if (bIsDestroyed || !MeshComponent || !IsValid(MeshComponent) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || HasAnyFlags(RF_ClassDefaultObject))
        return;

    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

    int32 x = FMath::FloorToInt((LocalPos.X + KINDA_SMALL_NUMBER) / CubeSize);
    int32 y = FMath::FloorToInt((LocalPos.Y + KINDA_SMALL_NUMBER) / CubeSize);
    int32 z = FMath::FloorToInt((LocalPos.Z + KINDA_SMALL_NUMBER) / CubeSize);

    if (z > 0 && GetVoxelAt(x, y, z) == EVoxelType::Air && GetVoxelAt(x, y, z - 1) == EVoxelType::Grass)
    {
        float GrassBaseZ = (z - 1) * CubeSize;
        if (LocalPos.Z > GrassBaseZ) z -= 1;
    }

    if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= MaxHeight)
    {
        UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel: OUT OF BOUNDS!"));
        return;
    }

    int32 Index = GetIndex(x, y, z);
    if (!VoxelData.IsValidIndex(Index) || VoxelData[Index] == EVoxelType::Air)
        return;

    VoxelData[Index] = EVoxelType::Air;
    UpdateVoxelRegion(x, y, z);
}

void ASmoothVoxelTerrain::PlaceVoxel(FVector WorldLocation, EVoxelType Type)
{
    if (bIsDestroyed || !MeshComponent || !IsValid(MeshComponent) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() || HasAnyFlags(RF_ClassDefaultObject))
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
    if (!VoxelData.IsValidIndex(Index) || VoxelData[Index] == Type)
        return;

    VoxelData[Index] = Type;
    UpdateVoxelRegion(x, y, z);
}

void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (bIsDestroyed || !MeshComponent || !IsValid(MeshComponent) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;
    GenerateVoxelData();
    CreateMesh();
}