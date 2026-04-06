#include "SmoothVoxelTerrain.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"

using namespace UE::Geometry;

static int32 FloorDiv(int32 Dividend, int32 Divisor)
{
    // C++ integer division truncates toward zero; we need floor (toward -inf)
    int32 Quotient = Dividend / Divisor;
    if ((Dividend ^ Divisor) < 0 && Dividend % Divisor != 0)
        Quotient--;
    return Quotient;
}

// -------------------------------------------------------------------
// Actor lifetime
// -------------------------------------------------------------------
ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = false;

    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = RootSceneComponent;
}

ASmoothVoxelTerrain::~ASmoothVoxelTerrain()
{
    bIsDestroyed = true;
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

    // Only generate if we don't have chunks, or if we are in a fresh game state
    if (Chunks.Num() == 0)
    {
        RebuildTerrain();
    }
}

void ASmoothVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bIsDestroyed = true;
    for (auto& Pair : Chunks)
    {
        if (Pair.Value->MeshComponent && IsValid(Pair.Value->MeshComponent))
        {
            Pair.Value->MeshComponent->DestroyComponent();
        }
    }
    Chunks.Empty();
    Super::EndPlay(EndPlayReason);
}

// -------------------------------------------------------------------
// Terrain generation – create all chunks
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::GenerateChunks()
{
    // 1. Properly Unregister and Destroy old components
    for (auto& Pair : Chunks)
    {
        if (Pair.Value && Pair.Value->MeshComponent)
        {
            // This is critical: Unregister before destroying
            Pair.Value->MeshComponent->UnregisterComponent();
            Pair.Value->MeshComponent->DestroyComponent();
        }
    }
    Chunks.Empty();

    // 2. Also clear any "stray" DynamicMeshComponents that might be hanging around
    TArray<UDynamicMeshComponent*> OldComps;
    GetComponents<UDynamicMeshComponent>(OldComps);
    for (UDynamicMeshComponent* Comp : OldComps)
    {
        Comp->UnregisterComponent();
        Comp->DestroyComponent();
    }

    // Create chunks in a grid
    for (int32 cx = 0; cx < WorldChunksX; ++cx)
    {
        for (int32 cy = 0; cy < WorldChunksY; ++cy)
        {
            FIntVector ChunkCoord(cx, cy, 0);
            TUniquePtr<FVoxelChunk> Chunk = MakeUnique<FVoxelChunk>();
            Chunk->Coord = ChunkCoord;
            Chunk->VoxelData.SetNumZeroed(ChunkSize * ChunkSize * MaxHeight);
            Chunk->VoxelTriangles.SetNum(Chunk->VoxelData.Num());

            // Fill voxel data using procedural heightmap
            for (int32 lx = 0; lx < ChunkSize; ++lx)
            {
                for (int32 ly = 0; ly < ChunkSize; ++ly)
                {
                    int32 WorldX = cx * ChunkSize + lx;
                    int32 WorldY = cy * ChunkSize + ly;

                    float h00 = GetHeightAtWorldCorner(WorldX, WorldY);
                    float h10 = GetHeightAtWorldCorner(WorldX + 1, WorldY);
                    float h01 = GetHeightAtWorldCorner(WorldX, WorldY + 1);
                    float h11 = GetHeightAtWorldCorner(WorldX + 1, WorldY + 1);
                    float MinCorner = FMath::Min3(h00, h10, FMath::Min(h01, h11));
                    int32 GroundLevel = FMath::Clamp(FMath::FloorToInt(MinCorner - MinGrassThickness), 0, MaxHeight - 1);

                    for (int32 lz = 0; lz < MaxHeight; ++lz)
                    {
                        int32 WorldZ = lz;
                        int32 Index = lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
                        if (WorldZ < GroundLevel - 3)
                            Chunk->VoxelData[Index] = EVoxelType::Stone;
                        else if (WorldZ < GroundLevel - 1)
                            Chunk->VoxelData[Index] = EVoxelType::Dirt;
                        else if (WorldZ == GroundLevel)
                            Chunk->VoxelData[Index] = EVoxelType::Grass;
                        else if (WorldZ < GroundLevel)
                            Chunk->VoxelData[Index] = EVoxelType::Dirt;
                        else
                            Chunk->VoxelData[Index] = EVoxelType::Air;
                    }
                }
            }

            // Create mesh component
            UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(this);
            MeshComp->CreationMethod = EComponentCreationMethod::UserConstructionScript;
            MeshComp->AttachToComponent(RootSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
            MeshComp->SetRelativeTransform(FTransform::Identity); // since Root is already at actor transform
            MeshComp->RegisterComponent();

            // Transform and attachment
            //MeshComp->SetWorldTransform(GetActorTransform());

            // Visibility and rendering
            MeshComp->SetVisibility(true);

            MeshComp->SetCastShadow(bCastShadow);
            MeshComp->SetReceivesDecals(bReceivesDecals);

            // Collision settings
            MeshComp->EnableComplexAsSimpleCollision();
            MeshComp->bEnableComplexCollision = bEnableComplexCollision;             // Enable complex collision
            MeshComp->SetCollisionEnabled(CollisionEnabled);                         // e.g., QueryAndPhysics
            MeshComp->SetCollisionProfileName(CollisionProfileName);                 // e.g., "BlockAll"
            MeshComp->SetGenerateOverlapEvents(bGenerateOverlapEvents);

            if (TerrainMaterial) MeshComp->SetMaterial(0, TerrainMaterial);
            Chunk->MeshComponent = MeshComp;

            Chunks.Add(ChunkCoord, MoveTemp(Chunk));
        }
    }

    // Build meshes for all chunks
    for (auto& Pair : Chunks)
    {
        Pair.Value->BuildMesh(this);
    }
}

void ASmoothVoxelTerrain::FVoxelChunk::BuildMesh(ASmoothVoxelTerrain* TerrainOwner)
{
    if (!MeshComponent) return;

    UDynamicMesh* DynamicMesh = MeshComponent->GetDynamicMesh();


    DynamicMesh->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            MeshOut.Clear();
            MeshOut.EnableAttributes();
            FDynamicMeshAttributeSet* Attr = MeshOut.Attributes();
            if (!Attr) return;
            Attr->SetNumUVLayers(1);

            for (auto& Arr : VoxelTriangles) Arr.Empty();

            for (int32 lx = 0; lx < TerrainOwner->ChunkSize; ++lx)
            {
                for (int32 ly = 0; ly < TerrainOwner->ChunkSize; ++ly)
                {
                    for (int32 lz = 0; lz < TerrainOwner->MaxHeight; ++lz)
                    {
                        int32 Index = lx + ly * TerrainOwner->ChunkSize + lz * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
                        if (VoxelData[Index] == EVoxelType::Air) continue;

                        int32 WorldX = Coord.X * TerrainOwner->ChunkSize + lx;
                        int32 WorldY = Coord.Y * TerrainOwner->ChunkSize + ly;
                        int32 WorldZ = lz;

                        TerrainOwner->AppendVoxelFacesWorld(WorldX, WorldY, WorldZ, MeshOut, VoxelTriangles[Index]);
                    }
                }
            }
        });

    MeshComponent->NotifyMeshUpdated();
    MeshComponent->UpdateCollision(true);
    // Force render state recreation
    MeshComponent->MarkRenderStateDirty();
    // Re-register component to ensure it's in the scene
    if (MeshComponent->IsRegistered())
        MeshComponent->ReregisterComponent();
    else
        MeshComponent->RegisterComponent();
}

void ASmoothVoxelTerrain::FVoxelChunk::UpdateVoxel(int32 LocalX, int32 LocalY, int32 LocalZ, EVoxelType NewType, ASmoothVoxelTerrain* TerrainOwner)
{
    int32 Index = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;

    UE_LOG(LogTemp, Warning, TEXT("Updating voxel: Local (%d,%d,%d), Index=%d, Old=%d, New=%d"),
        LocalX, LocalY, LocalZ, Index, (int)VoxelData[Index], (int)NewType);

    if (VoxelData[Index] == NewType) return;
    VoxelData[Index] = NewType;

    BuildMesh(TerrainOwner);
}

// -------------------------------------------------------------------
// Public edit functions (O(1) per chunk)
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::RemoveVoxel(FVector WorldLocation)
{
    if (bIsDestroyed) return;

    FIntVector ChunkCoord = WorldToChunkCoord(WorldLocation);
    FVoxelChunk* Chunk = GetChunk(ChunkCoord);
    if (!Chunk) return;

    int32 lx, ly, lz;
    WorldToLocalVoxel(WorldLocation, ChunkCoord, lx, ly, lz);
    if (lx < 0 || lx >= ChunkSize || ly < 0 || ly >= ChunkSize || lz < 0 || lz >= MaxHeight) return;

    int32 Index = lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
    if (Chunk->VoxelData[Index] == EVoxelType::Air) return;

    UE_LOG(LogTemp, Warning, TEXT("Removing voxel at chunk (%d,%d) local (%d,%d,%d)"),
        ChunkCoord.X, ChunkCoord.Y, lx, ly, lz);

    double Start = FPlatformTime::Seconds();
    Chunk->UpdateVoxel(lx, ly, lz, EVoxelType::Air, this);

    // If the edited voxel lies on a chunk boundary, also rebuild neighbor chunks.
    bool bOnBoundary = (lx == 0) || (lx == ChunkSize - 1) || (ly == 0) || (ly == ChunkSize - 1) || (lz == 0) || (lz == MaxHeight - 1);
    if (bOnBoundary)
    {
        for (int32 dx = -1; dx <= 1; ++dx)
        {
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                for (int32 dz = -1; dz <= 1; ++dz)
                {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    FIntVector NeighborCoord = ChunkCoord + FIntVector(dx, dy, dz);
                    FVoxelChunk* Neighbor = GetChunk(NeighborCoord);
                    if (Neighbor)
                        Neighbor->BuildMesh(this);
                }
            }
        }
    }

    double End = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning, TEXT("RemoveVoxel took %.2f ms"), (End - Start) * 1000.0);
}

void ASmoothVoxelTerrain::PlaceVoxel(FVector WorldLocation, EVoxelType Type)
{
    if (bIsDestroyed || Type == EVoxelType::Air) return;

    FIntVector ChunkCoord = WorldToChunkCoord(WorldLocation);
    FVoxelChunk* Chunk = GetChunk(ChunkCoord);
    if (!Chunk) return;

    int32 lx, ly, lz;
    WorldToLocalVoxel(WorldLocation, ChunkCoord, lx, ly, lz);
    if (lx < 0 || lx >= ChunkSize || ly < 0 || ly >= ChunkSize || lz < 0 || lz >= MaxHeight) return;

    Chunk->UpdateVoxel(lx, ly, lz, Type, this);

    // Rebuild neighbors if on boundary (same as RemoveVoxel)
    bool bOnBoundary = (lx == 0) || (lx == ChunkSize - 1) || (ly == 0) || (ly == ChunkSize - 1) || (lz == 0) || (lz == MaxHeight - 1);
    if (bOnBoundary)
    {
        for (int32 dx = -1; dx <= 1; ++dx)
        {
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                for (int32 dz = -1; dz <= 1; ++dz)
                {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    FIntVector NeighborCoord = ChunkCoord + FIntVector(dx, dy, dz);
                    FVoxelChunk* Neighbor = GetChunk(NeighborCoord);
                    if (Neighbor)
                        Neighbor->BuildMesh(this);
                }
            }
        }
    }
}

void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (bIsDestroyed) return;
    GenerateChunks();
}

// -------------------------------------------------------------------
// World <-> Chunk coordinate conversion
// -------------------------------------------------------------------
FIntVector ASmoothVoxelTerrain::WorldToChunkCoord(const FVector& WorldPos) const
{
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPos);
    int32 WorldX = FMath::FloorToInt(LocalPos.X / CubeSize);
    int32 WorldY = FMath::FloorToInt(LocalPos.Y / CubeSize);
    return FIntVector(
        FloorDiv(WorldX, ChunkSize),
        FloorDiv(WorldY, ChunkSize),
        0
    );
}
void ASmoothVoxelTerrain::WorldToLocalVoxel(const FVector& WorldPos, const FIntVector& ChunkCoord, int32& OutX, int32& OutY, int32& OutZ) const
{
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPos);
    int32 WorldX = FMath::FloorToInt(LocalPos.X / CubeSize);
    int32 WorldY = FMath::FloorToInt(LocalPos.Y / CubeSize);
    int32 WorldZ = FMath::FloorToInt(LocalPos.Z / CubeSize);
    OutX = WorldX - ChunkCoord.X * ChunkSize;
    OutY = WorldY - ChunkCoord.Y * ChunkSize;
    OutZ = WorldZ;
}

FVector ASmoothVoxelTerrain::ChunkCoordToWorldOrigin(const FIntVector& ChunkCoord) const
{
    FVector LocalOrigin(ChunkCoord.X * ChunkSize * CubeSize, ChunkCoord.Y * ChunkSize * CubeSize, 0.0f);
    return GetActorTransform().TransformPosition(LocalOrigin);
}

// -------------------------------------------------------------------
// Cross‑chunk voxel lookup
// -------------------------------------------------------------------
EVoxelType ASmoothVoxelTerrain::GetVoxelAtWorld(int32 WorldX, int32 WorldY, int32 WorldZ) const
{
    if (WorldZ < 0 || WorldZ >= MaxHeight) return EVoxelType::Air;

    int32 ChunkX = FloorDiv(WorldX, ChunkSize);
    int32 ChunkY = FloorDiv(WorldY, ChunkSize);
    FIntVector ChunkCoord(ChunkX, ChunkY, 0);
    const FVoxelChunk* Chunk = GetChunk(ChunkCoord);
    if (!Chunk) return EVoxelType::Air;

    int32 LocalX = WorldX - ChunkX * ChunkSize;
    int32 LocalY = WorldY - ChunkY * ChunkSize;
    if (LocalX < 0 || LocalX >= ChunkSize || LocalY < 0 || LocalY >= ChunkSize) return EVoxelType::Air;

    int32 Index = LocalX + LocalY * ChunkSize + WorldZ * ChunkSize * ChunkSize;
    if (!Chunk->VoxelData.IsValidIndex(Index)) return EVoxelType::Air;
    return Chunk->VoxelData[Index];
}

// -------------------------------------------------------------------
// Height sampling (procedural)
// -------------------------------------------------------------------
float ASmoothVoxelTerrain::GetHeightAtWorldCorner(int32 WorldX, int32 WorldY) const
{
    // Use Perlin noise based on world coordinates (scaled by CubeSize)
    float NoiseValue = FMath::PerlinNoise2D(FVector2D(WorldX + Seed, WorldY + Seed) * NoiseScale);
    return (NoiseValue + 1.0f) * HeightMultiplier / CubeSize; // return in voxel units
}

float ASmoothVoxelTerrain::GetInterpolatedHeight(float WorldX, float WorldY) const
{
    int32 x0 = FMath::FloorToInt(WorldX);
    int32 y0 = FMath::FloorToInt(WorldY);
    int32 x1 = x0 + 1;
    int32 y1 = y0 + 1;
    float fx = WorldX - x0;
    float fy = WorldY - y0;

    float h00 = GetHeightAtWorldCorner(x0, y0);
    float h10 = GetHeightAtWorldCorner(x1, y0);
    float h01 = GetHeightAtWorldCorner(x0, y1);
    float h11 = GetHeightAtWorldCorner(x1, y1);

    return FMath::Lerp(FMath::Lerp(h00, h10, fx), FMath::Lerp(h01, h11, fx), fy);
}

// -------------------------------------------------------------------
// Smooth vertex / normal helpers (world coordinate versions)
// -------------------------------------------------------------------
FVector ASmoothVoxelTerrain::GetSmoothVertexWorld(int32 WorldX, int32 WorldY, int32 WorldZ, int32 VoxX, int32 VoxY, int32 VoxZ) const
{
    if (!bSmoothTerrain)
        return FVector(WorldX, WorldY, WorldZ) * CubeSize;

    float TargetH = GetHeightAtWorldCorner(WorldX, WorldY);
    float FinalZ = (float)WorldZ;

    if (GetVoxelAtWorld(VoxX, VoxY, VoxZ) == EVoxelType::Grass && WorldZ > VoxZ)
    {
        if (GetVoxelAtWorld(VoxX, VoxY, VoxZ + 1) != EVoxelType::Air)
            return FVector(WorldX, WorldY, WorldZ) * CubeSize;
        FinalZ = TargetH;
    }

    return FVector(WorldX, WorldY, FinalZ) * CubeSize;
}

FVector ASmoothVoxelTerrain::GetSmoothNormalWorld(int32 WorldX, int32 WorldY) const
{
    float hL = GetHeightAtWorldCorner(WorldX - 1, WorldY);
    float hR = GetHeightAtWorldCorner(WorldX + 1, WorldY);
    float hD = GetHeightAtWorldCorner(WorldX, WorldY - 1);
    float hU = GetHeightAtWorldCorner(WorldX, WorldY + 1);
    return FVector(hL - hR, hD - hU, 2.0f).GetSafeNormal();
}

float ASmoothVoxelTerrain::GetNeighborTopHeightWorld(int32 WorldX, int32 WorldY, int32 WorldZ, const FVector& Vertex) const
{
    EVoxelType neighborType = GetVoxelAtWorld(WorldX, WorldY, WorldZ);
    if (neighborType == EVoxelType::Air)
        return -FLT_MAX;
    else if (neighborType == EVoxelType::Grass)
        return GetInterpolatedHeight(Vertex.X / CubeSize, Vertex.Y / CubeSize);
    else
        return (WorldZ + 1) * CubeSize;
}

// -------------------------------------------------------------------
// Append faces for a single voxel (world coordinates)
// -------------------------------------------------------------------
void ASmoothVoxelTerrain::AppendVoxelFacesWorld(int32 WorldX, int32 WorldY, int32 WorldZ, FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs)
{
    FDynamicMeshAttributeSet* Attr = Mesh.Attributes();
    if (!Attr) return;
    FDynamicMeshUVOverlay* UVOverlay = Attr->GetUVLayer(0);
    FDynamicMeshNormalOverlay* NormalOverlay = Attr->PrimaryNormals();
    if (!UVOverlay || !NormalOverlay) return;

    // Compute 8 corners using world coordinates
    FVector v000 = GetSmoothVertexWorld(WorldX, WorldY, WorldZ, WorldX, WorldY, WorldZ);
    FVector v100 = GetSmoothVertexWorld(WorldX + 1, WorldY, WorldZ, WorldX, WorldY, WorldZ);
    FVector v010 = GetSmoothVertexWorld(WorldX, WorldY + 1, WorldZ, WorldX, WorldY, WorldZ);
    FVector v110 = GetSmoothVertexWorld(WorldX + 1, WorldY + 1, WorldZ, WorldX, WorldY, WorldZ);
    FVector v001 = GetSmoothVertexWorld(WorldX, WorldY, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v101 = GetSmoothVertexWorld(WorldX + 1, WorldY, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v011 = GetSmoothVertexWorld(WorldX, WorldY + 1, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v111 = GetSmoothVertexWorld(WorldX + 1, WorldY + 1, WorldZ + 1, WorldX, WorldY, WorldZ);

    // Lambda to add a quad (two triangles) with normals and UVs
    auto AddQuad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D,
        const FVector& NormalA, const FVector& NormalB, const FVector& NormalC, const FVector& NormalD,
        const FVector2D& UV_A, const FVector2D& UV_B, const FVector2D& UV_C, const FVector2D& UV_D)
        {
            int32 vA = Mesh.AppendVertex(FVector3d(A));
            int32 vB = Mesh.AppendVertex(FVector3d(B));
            int32 vC = Mesh.AppendVertex(FVector3d(C));
            int32 vD = Mesh.AppendVertex(FVector3d(D));

            int32 t1 = Mesh.AppendTriangle(vA, vB, vC);
            if (t1 != FDynamicMesh3::InvalidID)
            {
                OutTriIDs.Add(t1);
                int32 nA = NormalOverlay->AppendElement(FVector3f(NormalA));
                int32 nB = NormalOverlay->AppendElement(FVector3f(NormalB));
                int32 nC = NormalOverlay->AppendElement(FVector3f(NormalC));
                NormalOverlay->SetTriangle(t1, FIndex3i(nA, nB, nC));
                int32 uvA = UVOverlay->AppendElement(FVector2f(UV_A));
                int32 uvB = UVOverlay->AppendElement(FVector2f(UV_B));
                int32 uvC = UVOverlay->AppendElement(FVector2f(UV_C));
                UVOverlay->SetTriangle(t1, FIndex3i(uvA, uvB, uvC));
            }

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

    auto AddQuadFlat = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal)
        {
            AddQuad(A, B, C, D, Normal, Normal, Normal, Normal,
                FVector2D(0, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1));
        };

    // Top face (+Z)
    if (GetVoxelAtWorld(WorldX, WorldY, WorldZ + 1) == EVoxelType::Air)
    {
        if (bSmoothTerrain)
        {
            AddQuad(v001, v011, v111, v101,
                GetSmoothNormalWorld(WorldX, WorldY),
                GetSmoothNormalWorld(WorldX, WorldY + 1),
                GetSmoothNormalWorld(WorldX + 1, WorldY + 1),
                GetSmoothNormalWorld(WorldX + 1, WorldY),
                FVector2D(0, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1));
        }
        else
        {
            AddQuadFlat(v001, v011, v111, v101, FVector::UpVector);
        }
    }

    // Bottom face (-Z)
    if (GetVoxelAtWorld(WorldX, WorldY, WorldZ - 1) == EVoxelType::Air)
        AddQuadFlat(v100, v110, v010, v000, FVector::DownVector);

    // +X face
    if (GetVoxelAtWorld(WorldX + 1, WorldY, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v100) < v100.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v101) < v101.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v111) < v111.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v110) < v110.Z)
    {
        AddQuadFlat(v100, v101, v111, v110, FVector::RightVector);
    }

    // -X face
    if (GetVoxelAtWorld(WorldX - 1, WorldY, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v010) < v010.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v011) < v011.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v001) < v001.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v000) < v000.Z)
    {
        AddQuadFlat(v010, v011, v001, v000, FVector::LeftVector);
    }

    // +Y face
    if (GetVoxelAtWorld(WorldX, WorldY + 1, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v110) < v110.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v111) < v111.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v011) < v011.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v010) < v010.Z)
    {
        AddQuadFlat(v110, v111, v011, v010, FVector::ForwardVector);
    }

    // -Y face
    if (GetVoxelAtWorld(WorldX, WorldY - 1, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v000) < v000.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v001) < v001.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v101) < v101.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v100) < v100.Z)
    {
        AddQuadFlat(v000, v001, v101, v100, FVector::BackwardVector);
    }
}

// -------------------------------------------------------------------
// Chunk access helpers
// -------------------------------------------------------------------
ASmoothVoxelTerrain::FVoxelChunk* ASmoothVoxelTerrain::GetChunk(const FIntVector& Coord)
{
    if (auto* Ptr = Chunks.Find(Coord)) return Ptr->Get();
    return nullptr;
}

const ASmoothVoxelTerrain::FVoxelChunk* ASmoothVoxelTerrain::GetChunk(const FIntVector& Coord) const
{
    if (auto* Ptr = Chunks.Find(Coord)) return Ptr->Get();
    return nullptr;
}

#if WITH_EDITOR
void ASmoothVoxelTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // If any of the relevant properties changed, refresh all chunks
    static const TArray<FName> RelevantProperties = {
        GET_MEMBER_NAME_CHECKED(ASmoothVoxelTerrain, CollisionEnabled),
        GET_MEMBER_NAME_CHECKED(ASmoothVoxelTerrain, CollisionProfileName),
        GET_MEMBER_NAME_CHECKED(ASmoothVoxelTerrain, bGenerateOverlapEvents),
        GET_MEMBER_NAME_CHECKED(ASmoothVoxelTerrain, bCastShadow),
        GET_MEMBER_NAME_CHECKED(ASmoothVoxelTerrain, bReceivesDecals)
    };
    if (RelevantProperties.Contains(PropertyChangedEvent.GetPropertyName()))
    {
        for (auto& Pair : Chunks)
        {
            if (Pair.Value->MeshComponent)
            {
                Pair.Value->MeshComponent->SetCollisionEnabled(CollisionEnabled);
                Pair.Value->MeshComponent->SetCollisionProfileName(CollisionProfileName);
                Pair.Value->MeshComponent->SetGenerateOverlapEvents(bGenerateOverlapEvents);
                Pair.Value->MeshComponent->SetCastShadow(bCastShadow);
                Pair.Value->MeshComponent->SetReceivesDecals(bReceivesDecals);
            }
        }
    }
}
#endif