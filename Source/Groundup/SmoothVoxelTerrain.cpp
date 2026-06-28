#include "SmoothVoxelTerrain.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"

using namespace UE::Geometry;

static int32 FloorDiv(int32 Dividend, int32 Divisor)
{
    int32 Quotient = Dividend / Divisor;
    if ((Dividend ^ Divisor) < 0 && Dividend % Divisor != 0)
        Quotient--;
    return Quotient;
}

ASmoothVoxelTerrain::ASmoothVoxelTerrain()
{
    PrimaryActorTick.bCanEverTick = true;
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

    // Safety checks ensuring template/archetype duplicates do not initialize the generator
    if (bIsDestroyed || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed())
        return;

    RebuildTerrain();
}

void ASmoothVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
    if (Chunks.Num() == 0)
    {
        RebuildTerrain();
    }
}

void ASmoothVoxelTerrain::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void ASmoothVoxelTerrain::UpdateCollisionIfNeeded()
{
    if (bCollisionDirty)
    {
        for (auto& Pair : Chunks)
        {
            if (Pair.Value->MeshComponent)
                Pair.Value->MeshComponent->UpdateCollision(false);
        }
        bCollisionDirty = false;
    }
}

void ASmoothVoxelTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bIsDestroyed = true;
    for (auto& Pair : Chunks)
    {
        if (Pair.Value)
        {
            if (Pair.Value->MeshComponent && IsValid(Pair.Value->MeshComponent))
            {
                Pair.Value->MeshComponent->DestroyComponent();
            }
            if (Pair.Value->GrassMeshComponent && IsValid(Pair.Value->GrassMeshComponent))
            {
                Pair.Value->GrassMeshComponent->DestroyComponent();
            }
        }
    }
    Chunks.Empty();
    Super::EndPlay(EndPlayReason);
}

void ASmoothVoxelTerrain::GenerateChunks()
{
    for (auto& Pair : Chunks)
    {
        if (Pair.Value)
        {
            if (Pair.Value->MeshComponent)
            {
                Pair.Value->MeshComponent->UnregisterComponent();
                Pair.Value->MeshComponent->DestroyComponent();
            }
            if (Pair.Value->GrassMeshComponent)
            {
                Pair.Value->GrassMeshComponent->UnregisterComponent();
                Pair.Value->GrassMeshComponent->DestroyComponent();
            }
        }
    }
    Chunks.Empty();

    TArray<UDynamicMeshComponent*> OldComps;
    GetComponents<UDynamicMeshComponent>(OldComps);
    for (UDynamicMeshComponent* Comp : OldComps)
    {
        Comp->UnregisterComponent();
        Comp->DestroyComponent();
    }

    for (int32 cx = 0; cx < WorldChunksX; ++cx)
    {
        for (int32 cy = 0; cy < WorldChunksY; ++cy)
        {
            FIntVector ChunkCoord(cx, cy, 0);
            TUniquePtr<FVoxelChunk> Chunk = MakeUnique<FVoxelChunk>();
            Chunk->Coord = ChunkCoord;
            Chunk->VoxelData.SetNumZeroed(ChunkSize * ChunkSize * MaxHeight);
            Chunk->VoxelTriangles.SetNum(Chunk->VoxelData.Num());
            Chunk->GrassVoxelTriangles.SetNum(Chunk->VoxelData.Num());

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

            // 1. Terrain Mesh Component
            UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(this);
            MeshComp->CreationMethod = EComponentCreationMethod::Instance;
            MeshComp->AttachToComponent(RootSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
            MeshComp->SetRelativeTransform(FTransform()); // Replaced static FTransform::Identity
            MeshComp->RegisterComponent();

            MeshComp->SetVisibility(true);
            MeshComp->SetCastShadow(bCastShadow);
            MeshComp->SetReceivesDecals(bReceivesDecals);

            MeshComp->EnableComplexAsSimpleCollision();
            MeshComp->bEnableComplexCollision = bEnableComplexCollision;
            MeshComp->SetCollisionEnabled(CollisionEnabled);
            MeshComp->SetCollisionProfileName(CollisionProfileName);
            MeshComp->SetGenerateOverlapEvents(bGenerateOverlapEvents);

            if (TerrainMaterial) MeshComp->SetMaterial(0, TerrainMaterial);
            Chunk->MeshComponent = MeshComp;

            // 2. Grass Blades Component (Shadow casting and collision disabled)
            UDynamicMeshComponent* GrassMeshComp = NewObject<UDynamicMeshComponent>(this);
            GrassMeshComp->CreationMethod = EComponentCreationMethod::Instance;
            GrassMeshComp->AttachToComponent(RootSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
            GrassMeshComp->SetRelativeTransform(FTransform()); // Replaced static FTransform::Identity
            GrassMeshComp->RegisterComponent();

            GrassMeshComp->SetVisibility(true);
            GrassMeshComp->SetCastShadow(false); // No grass shadows
            GrassMeshComp->SetReceivesDecals(false);

            GrassMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision); // No grass collision
            GrassMeshComp->bEnableComplexCollision = false;

            if (TerrainMaterial) GrassMeshComp->SetMaterial(0, TerrainMaterial);
            Chunk->GrassMeshComponent = GrassMeshComp;

            Chunks.Add(ChunkCoord, MoveTemp(Chunk));
        }
    }

    for (auto& Pair : Chunks)
    {
        Pair.Value->BuildMesh(this);
    }
}

void ASmoothVoxelTerrain::FVoxelChunk::BuildMesh(ASmoothVoxelTerrain* TerrainOwner)
{
    if (!MeshComponent || !GrassMeshComponent) return;

    UDynamicMesh* DynamicMesh = MeshComponent->GetDynamicMesh();
    UDynamicMesh* GrassDynamicMesh = GrassMeshComponent->GetDynamicMesh();

    // 1. Render Voxel Terrain Faces
    DynamicMesh->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            MeshOut.Clear();
            MeshOut.EnableAttributes();
            FDynamicMeshAttributeSet* Attr = MeshOut.Attributes();
            if (!Attr) return;
            Attr->SetNumUVLayers(2);

            if (!Attr->PrimaryColors())
            {
                Attr->EnablePrimaryColors();
            }

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

    // 2. Render Voxel Grass Geometry (Populate companion grass mesh)
    GrassDynamicMesh->EditMesh([&](FDynamicMesh3& GrassMeshOut)
        {
            GrassMeshOut.Clear();
            GrassMeshOut.EnableAttributes();
            FDynamicMeshAttributeSet* Attr = GrassMeshOut.Attributes();
            if (!Attr) return;
            Attr->SetNumUVLayers(2);

            if (!Attr->PrimaryColors())
            {
                Attr->EnablePrimaryColors();
            }

            for (auto& Arr : GrassVoxelTriangles) Arr.Empty();

            if (TerrainOwner->bEnableGrassGeometry)
            {
                for (int32 lx = 0; lx < TerrainOwner->ChunkSize; ++lx)
                {
                    for (int32 ly = 0; ly < TerrainOwner->ChunkSize; ++ly)
                    {
                        for (int32 lz = 0; lz < TerrainOwner->MaxHeight; ++lz)
                        {
                            int32 Index = lx + ly * TerrainOwner->ChunkSize + lz * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
                            if (VoxelData[Index] != EVoxelType::Grass) continue;

                            int32 WorldX = Coord.X * TerrainOwner->ChunkSize + lx;
                            int32 WorldY = Coord.Y * TerrainOwner->ChunkSize + ly;
                            int32 WorldZ = lz;

                            if (TerrainOwner->GetVoxelAtWorld(WorldX, WorldY, WorldZ + 1) == EVoxelType::Air)
                            {
                                TerrainOwner->AppendGrassBladesWorld(WorldX, WorldY, WorldZ, GrassMeshOut, GrassVoxelTriangles[Index]);
                            }
                        }
                    }
                }
            }
        });

    if (MeshComponent->IsRegistered())
        MeshComponent->ReregisterComponent();
    else
        MeshComponent->RegisterComponent();

    if (GrassMeshComponent->IsRegistered())
        GrassMeshComponent->ReregisterComponent();
    else
        GrassMeshComponent->RegisterComponent();
}

void ASmoothVoxelTerrain::FVoxelChunk::UpdateSharedFace(int32 LocalX, int32 LocalY, int32 LocalZ, ASmoothVoxelTerrain* TerrainOwner, const FIntVector& NeighborDirection)
{
    int32 Index = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
    if (VoxelData[Index] == EVoxelType::Air) return;

    UDynamicMesh* DynamicMesh = MeshComponent->GetDynamicMesh();
    UDynamicMesh* GrassDynamicMesh = GrassMeshComponent->GetDynamicMesh();
    if (!DynamicMesh || !GrassDynamicMesh) return;

    DynamicMesh->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            GrassDynamicMesh->EditMesh([&](FDynamicMesh3& GrassMeshOut)
                {
                    RemoveVoxelFaces(LocalX, LocalY, LocalZ, MeshOut, GrassMeshOut, TerrainOwner);
                    AddVoxelFaces(LocalX, LocalY, LocalZ, MeshOut, GrassMeshOut, TerrainOwner);
                });
        });
}

void ASmoothVoxelTerrain::FVoxelChunk::UpdateVoxel(int32 LocalX, int32 LocalY, int32 LocalZ, EVoxelType NewType, ASmoothVoxelTerrain* TerrainOwner)
{
    int32 Index = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
    if (VoxelData[Index] == NewType) return;

    UpdateVoxelMesh(LocalX, LocalY, LocalZ, NewType, TerrainOwner);
}

bool ASmoothVoxelTerrain::GetVoxelAtWorldPoint(const FVector& WorldPoint,
    int32& OutVoxelX, int32& OutVoxelY, int32& OutVoxelZ,
    EVoxelType* OutType)
{
    FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPoint);

    OutVoxelX = FMath::FloorToInt(LocalPos.X / CubeSize);
    OutVoxelY = FMath::FloorToInt(LocalPos.Y / CubeSize);
    OutVoxelZ = FMath::FloorToInt(LocalPos.Z / CubeSize);

    const int32 MaxVoxelX = WorldChunksX * ChunkSize;
    const int32 MaxVoxelY = WorldChunksY * ChunkSize;

    if (OutVoxelX < 0 || OutVoxelX >= MaxVoxelX ||
        OutVoxelY < 0 || OutVoxelY >= MaxVoxelY ||
        OutVoxelZ < 0 || OutVoxelZ >= MaxHeight)
    {
        return false;
    }

    if (OutType)
    {
        *OutType = GetVoxelAtWorld(OutVoxelX, OutVoxelY, OutVoxelZ);
    }

    return true;
}

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
    if (Chunk->VoxelData[Index] == EVoxelType::Air) {
        return;
    };

    double Start = FPlatformTime::Seconds();

    Chunk->UpdateVoxel(lx, ly, lz, EVoxelType::Air, this);

    if (lx == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(-1, 0, 0)))
            Neighbor->UpdateSharedFace(ChunkSize - 1, ly, lz, this, FIntVector(1, 0, 0));
    }
    if (lx == ChunkSize - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(1, 0, 0)))
            Neighbor->UpdateSharedFace(0, ly, lz, this, FIntVector(-1, 0, 0));
    }
    if (ly == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, -1, 0)))
            Neighbor->UpdateSharedFace(lx, ChunkSize - 1, lz, this, FIntVector(0, 1, 0));
    }
    if (ly == ChunkSize - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 1, 0)))
            Neighbor->UpdateSharedFace(lx, 0, lz, this, FIntVector(0, -1, 0));
    }
    if (lz == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 0, -1)))
            Neighbor->UpdateSharedFace(lx, ly, MaxHeight - 1, this, FIntVector(0, 0, 1));
    }
    if (lz == MaxHeight - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 0, 1)))
            Neighbor->UpdateSharedFace(lx, ly, 0, this, FIntVector(0, 0, -1));
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

    if (lx == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(-1, 0, 0)))
            Neighbor->UpdateSharedFace(ChunkSize - 1, ly, lz, this, FIntVector(1, 0, 0));
    }
    if (lx == ChunkSize - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(1, 0, 0)))
            Neighbor->UpdateSharedFace(0, ly, lz, this, FIntVector(-1, 0, 0));
    }
    if (ly == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, -1, 0)))
            Neighbor->UpdateSharedFace(lx, ChunkSize - 1, lz, this, FIntVector(0, 1, 0));
    }
    if (ly == ChunkSize - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 1, 0)))
            Neighbor->UpdateSharedFace(lx, 0, lz, this, FIntVector(0, -1, 0));
    }
    if (lz == 0) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 0, -1)))
            Neighbor->UpdateSharedFace(lx, ly, MaxHeight - 1, this, FIntVector(0, 0, 1));
    }
    if (lz == MaxHeight - 1) {
        if (FVoxelChunk* Neighbor = GetChunk(ChunkCoord + FIntVector(0, 0, 1)))
            Neighbor->UpdateSharedFace(lx, ly, 0, this, FIntVector(0, 0, -1));
    }
}

void ASmoothVoxelTerrain::RebuildTerrain()
{
    if (bIsDestroyed) return;
    GenerateChunks();
}

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

float ASmoothVoxelTerrain::GetHeightAtWorldCorner(int32 WorldX, int32 WorldY) const
{
    float NoiseValue = FMath::PerlinNoise2D(FVector2D(WorldX + Seed, WorldY + Seed) * NoiseScale);
    return (NoiseValue + 1.0f) * HeightMultiplier / CubeSize;
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

FLinearColor ASmoothVoxelTerrain::GetStylizedColorForVoxel(int32 WorldX, int32 WorldY, int32 WorldZ, EVoxelType VoxelType) const
{
    if (VoxelType == EVoxelType::Grass)
    {
        float ColorNoise = FMath::PerlinNoise2D(FVector2D((float)WorldX, (float)WorldY) * GrassColorNoiseScale);
        ColorNoise = FMath::Clamp((ColorNoise + 1.0f) * 0.5f, 0.0f, 1.0f);
        return FLinearColor::LerpUsingHSV(GrassBaseColorDark, GrassBaseColorLight, ColorNoise);
    }
    else if (VoxelType == EVoxelType::Dirt)
    {
        float DirtNoise = FMath::PerlinNoise2D(FVector2D((float)WorldX, (float)WorldY) * 0.1f);
        FLinearColor DirtDark(0.12f, 0.07f, 0.05f, 1.0f);
        FLinearColor DirtLight(0.20f, 0.12f, 0.08f, 1.0f);
        return FLinearColor::LerpUsingHSV(DirtDark, DirtLight, (DirtNoise + 1.0f) * 0.5f);
    }
    else if (VoxelType == EVoxelType::Stone)
    {
        float StoneNoise = FMath::PerlinNoise2D(FVector2D((float)WorldX, (float)WorldY) * 0.08f);
        FLinearColor StoneDark(0.18f, 0.20f, 0.22f, 1.0f);
        FLinearColor StoneLight(0.30f, 0.32f, 0.34f, 1.0f);
        return FLinearColor::LerpUsingHSV(StoneDark, StoneLight, (StoneNoise + 1.0f) * 0.5f);
    }
    return FLinearColor::White;
}

void ASmoothVoxelTerrain::AppendVoxelFacesWorld(int32 WorldX, int32 WorldY, int32 WorldZ, FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs)
{
    FDynamicMeshAttributeSet* Attr = Mesh.Attributes();
    if (!Attr) return;
    FDynamicMeshUVOverlay* UVOverlay = Attr->GetUVLayer(0);
    FDynamicMeshNormalOverlay* NormalOverlay = Attr->PrimaryNormals();
    FDynamicMeshColorOverlay* ColorOverlay = Attr->PrimaryColors();
    if (!UVOverlay || !NormalOverlay || !ColorOverlay) return;

    FVector v000 = GetSmoothVertexWorld(WorldX, WorldY, WorldZ, WorldX, WorldY, WorldZ);
    FVector v100 = GetSmoothVertexWorld(WorldX + 1, WorldY, WorldZ, WorldX, WorldY, WorldZ);
    FVector v010 = GetSmoothVertexWorld(WorldX, WorldY + 1, WorldZ, WorldX, WorldY, WorldZ);
    FVector v110 = GetSmoothVertexWorld(WorldX + 1, WorldY + 1, WorldZ, WorldX, WorldY, WorldZ);
    FVector v001 = GetSmoothVertexWorld(WorldX, WorldY, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v101 = GetSmoothVertexWorld(WorldX + 1, WorldY, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v011 = GetSmoothVertexWorld(WorldX, WorldY + 1, WorldZ + 1, WorldX, WorldY, WorldZ);
    FVector v111 = GetSmoothVertexWorld(WorldX + 1, WorldY + 1, WorldZ + 1, WorldX, WorldY, WorldZ);

    auto GetUVForVertex = [&](const FVector& Pos, const FVector& FaceNormal) -> FVector2D
        {
            FVector AbsN = FaceNormal.GetAbs();
            float U, V;
            if (AbsN.Z > 0.9f)
            {
                U = Pos.X * TextureScale;
                V = Pos.Y * TextureScale;
            }
            else if (AbsN.X > 0.9f)
            {
                U = Pos.Y * TextureScale;
                V = Pos.Z * TextureScale;
            }
            else
            {
                U = Pos.X * TextureScale;
                V = Pos.Z * TextureScale;
            }
            return FVector2D(U, V);
        };

    auto ComputeTriangleNormal = [](const FVector& A, const FVector& B, const FVector& C) -> FVector
        {
            return FVector::CrossProduct(C - A, B - A).GetSafeNormal();
        };

    auto AddQuadWorld = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D,
        const FVector& FaceNormal, EVoxelType VType)
        {
            FVector2D uvA = GetUVForVertex(A, FaceNormal);
            FVector2D uvB = GetUVForVertex(B, FaceNormal);
            FVector2D uvC = GetUVForVertex(C, FaceNormal);
            FVector2D uvD = GetUVForVertex(D, FaceNormal);

            FLinearColor colA = GetStylizedColorForVoxel(FMath::RoundToInt(A.X / CubeSize), FMath::RoundToInt(A.Y / CubeSize), FMath::RoundToInt(A.Z / CubeSize), VType);
            FLinearColor colB = GetStylizedColorForVoxel(FMath::RoundToInt(B.X / CubeSize), FMath::RoundToInt(B.Y / CubeSize), FMath::RoundToInt(B.Z / CubeSize), VType);
            FLinearColor colC = GetStylizedColorForVoxel(FMath::RoundToInt(C.X / CubeSize), FMath::RoundToInt(C.Y / CubeSize), FMath::RoundToInt(C.Z / CubeSize), VType);
            FLinearColor colD = GetStylizedColorForVoxel(FMath::RoundToInt(D.X / CubeSize), FMath::RoundToInt(D.Y / CubeSize), FMath::RoundToInt(D.Z / CubeSize), VType);

            int32 vA = Mesh.AppendVertex(FVector3d(A));
            int32 vB = Mesh.AppendVertex(FVector3d(B));
            int32 vC = Mesh.AppendVertex(FVector3d(C));
            int32 vD = Mesh.AppendVertex(FVector3d(D));

            int32 cA = ColorOverlay->AppendElement(FVector4f(colA));
            int32 cB = ColorOverlay->AppendElement(FVector4f(colB));
            int32 cC = ColorOverlay->AppendElement(FVector4f(colC));
            int32 cD = ColorOverlay->AppendElement(FVector4f(colD));

            FVector n1 = ComputeTriangleNormal(A, B, C);
            int32 t1 = Mesh.AppendTriangle(vA, vB, vC);
            if (t1 != FDynamicMesh3::InvalidID)
            {
                OutTriIDs.Add(t1);
                int32 nA1 = NormalOverlay->AppendElement(FVector3f(n1));
                int32 nB1 = NormalOverlay->AppendElement(FVector3f(n1));
                int32 nC1 = NormalOverlay->AppendElement(FVector3f(n1));
                NormalOverlay->SetTriangle(t1, FIndex3i(nA1, nB1, nC1));

                int32 uvA1 = UVOverlay->AppendElement(FVector2f(uvA));
                int32 uvB1 = UVOverlay->AppendElement(FVector2f(uvB));
                int32 uvC1 = UVOverlay->AppendElement(FVector2f(uvC));
                UVOverlay->SetTriangle(t1, FIndex3i(uvA1, uvB1, uvC1));

                ColorOverlay->SetTriangle(t1, FIndex3i(cA, cB, cC));
            }

            FVector n2 = ComputeTriangleNormal(A, C, D);
            int32 t2 = Mesh.AppendTriangle(vA, vC, vD);
            if (t2 != FDynamicMesh3::InvalidID)
            {
                OutTriIDs.Add(t2);
                int32 nA2 = NormalOverlay->AppendElement(FVector3f(n2));
                int32 nC2 = NormalOverlay->AppendElement(FVector3f(n2));
                int32 nD2 = NormalOverlay->AppendElement(FVector3f(n2));
                NormalOverlay->SetTriangle(t2, FIndex3i(nA2, nC2, nD2));

                int32 uvA2 = UVOverlay->AppendElement(FVector2f(uvA));
                int32 uvC2 = UVOverlay->AppendElement(FVector2f(uvC));
                int32 uvD2 = UVOverlay->AppendElement(FVector2f(uvD));
                UVOverlay->SetTriangle(t2, FIndex3i(uvA2, uvC2, uvD2));

                ColorOverlay->SetTriangle(t2, FIndex3i(cA, cC, cD));
            }
        };

    EVoxelType CurrentType = GetVoxelAtWorld(WorldX, WorldY, WorldZ);
    bool bExposedTop = GetVoxelAtWorld(WorldX, WorldY, WorldZ + 1) == EVoxelType::Air;

    if (bExposedTop)
    {
        if (bSmoothTerrain)
        {
            FVector n00 = GetSmoothNormalWorld(WorldX, WorldY);
            FVector n10 = GetSmoothNormalWorld(WorldX + 1, WorldY);
            FVector n01 = GetSmoothNormalWorld(WorldX, WorldY + 1);
            FVector n11 = GetSmoothNormalWorld(WorldX + 1, WorldY + 1);

            auto AddTopQuadSmooth = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D,
                const FVector& nA, const FVector& nB, const FVector& nC, const FVector& nD, EVoxelType VType)
                {
                    FVector2D uvA = GetUVForVertex(A, FVector(0.f, 0.f, 1.f)); // Replaced FVector::UpVector
                    FVector2D uvB = GetUVForVertex(B, FVector(0.f, 0.f, 1.f));
                    FVector2D uvC = GetUVForVertex(C, FVector(0.f, 0.f, 1.f));
                    FVector2D uvD = GetUVForVertex(D, FVector(0.f, 0.f, 1.f));

                    FLinearColor colA = GetStylizedColorForVoxel(FMath::RoundToInt(A.X / CubeSize), FMath::RoundToInt(A.Y / CubeSize), FMath::RoundToInt(A.Z / CubeSize), VType);
                    FLinearColor colB = GetStylizedColorForVoxel(FMath::RoundToInt(B.X / CubeSize), FMath::RoundToInt(B.Y / CubeSize), FMath::RoundToInt(B.Z / CubeSize), VType);
                    FLinearColor colC = GetStylizedColorForVoxel(FMath::RoundToInt(C.X / CubeSize), FMath::RoundToInt(C.Y / CubeSize), FMath::RoundToInt(C.Z / CubeSize), VType);
                    FLinearColor colD = GetStylizedColorForVoxel(FMath::RoundToInt(D.X / CubeSize), FMath::RoundToInt(D.Y / CubeSize), FMath::RoundToInt(D.Z / CubeSize), VType);

                    int32 vA = Mesh.AppendVertex(FVector3d(A));
                    int32 vB = Mesh.AppendVertex(FVector3d(B));
                    int32 vC = Mesh.AppendVertex(FVector3d(C));
                    int32 vD = Mesh.AppendVertex(FVector3d(D));

                    int32 cA = ColorOverlay->AppendElement(FVector4f(colA));
                    int32 cB = ColorOverlay->AppendElement(FVector4f(colB));
                    int32 cC = ColorOverlay->AppendElement(FVector4f(colC));
                    int32 cD = ColorOverlay->AppendElement(FVector4f(colD));

                    int32 t1 = Mesh.AppendTriangle(vA, vB, vC);
                    if (t1 != FDynamicMesh3::InvalidID)
                    {
                        OutTriIDs.Add(t1);
                        int32 nA1 = NormalOverlay->AppendElement(FVector3f(nA));
                        int32 nB1 = NormalOverlay->AppendElement(FVector3f(nB));
                        int32 nC1 = NormalOverlay->AppendElement(FVector3f(nC));
                        NormalOverlay->SetTriangle(t1, FIndex3i(nA1, nB1, nC1));
                        int32 uvA1 = UVOverlay->AppendElement(FVector2f(uvA));
                        int32 uvB1 = UVOverlay->AppendElement(FVector2f(uvB));
                        int32 uvC1 = UVOverlay->AppendElement(FVector2f(uvC));
                        UVOverlay->SetTriangle(t1, FIndex3i(uvA1, uvB1, uvC1));

                        ColorOverlay->SetTriangle(t1, FIndex3i(cA, cB, cC));
                    }
                    int32 t2 = Mesh.AppendTriangle(vA, vC, vD);
                    if (t2 != FDynamicMesh3::InvalidID)
                    {
                        OutTriIDs.Add(t2);
                        int32 nA2 = NormalOverlay->AppendElement(FVector3f(nA));
                        int32 nC2 = NormalOverlay->AppendElement(FVector3f(nA));
                        int32 nD2 = NormalOverlay->AppendElement(FVector3f(nD));
                        NormalOverlay->SetTriangle(t2, FIndex3i(nA2, nC2, nD2));
                        int32 uvA2 = UVOverlay->AppendElement(FVector2f(uvA));
                        int32 uvC2 = UVOverlay->AppendElement(FVector2f(uvC));
                        int32 uvD2 = UVOverlay->AppendElement(FVector2f(uvD));
                        UVOverlay->SetTriangle(t2, FIndex3i(uvA2, uvC2, uvD2));

                        ColorOverlay->SetTriangle(t2, FIndex3i(cA, cC, cD));
                    }
                };
            AddTopQuadSmooth(v001, v011, v111, v101, n00, n01, n11, n10, CurrentType);
        }
        else
        {
            AddQuadWorld(v001, v011, v111, v101, FVector(0.f, 0.f, 1.f), CurrentType); // Replaced FVector::UpVector
        }
    }

    if (GetVoxelAtWorld(WorldX, WorldY, WorldZ - 1) == EVoxelType::Air)
        AddQuadWorld(v100, v110, v010, v000, FVector(0.f, 0.f, -1.f), CurrentType); // Replaced FVector::DownVector

    if (GetVoxelAtWorld(WorldX + 1, WorldY, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v100) < v100.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v101) < v101.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v111) < v111.Z ||
        GetNeighborTopHeightWorld(WorldX + 1, WorldY, WorldZ, v110) < v110.Z)
    {
        AddQuadWorld(v100, v101, v111, v110, FVector(0.f, 1.f, 0.f), CurrentType); // Replaced FVector::RightVector
    }

    if (GetVoxelAtWorld(WorldX - 1, WorldY, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v010) < v010.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v011) < v011.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v001) < v001.Z ||
        GetNeighborTopHeightWorld(WorldX - 1, WorldY, WorldZ, v000) < v000.Z)
    {
        AddQuadWorld(v010, v011, v001, v000, FVector(0.f, -1.f, 0.f), CurrentType); // Replaced FVector::LeftVector
    }

    if (GetVoxelAtWorld(WorldX, WorldY + 1, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v110) < v110.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v111) < v111.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v011) < v011.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY + 1, WorldZ, v010) < v010.Z)
    {
        AddQuadWorld(v110, v111, v011, v010, FVector(1.f, 0.f, 0.f), CurrentType); // Replaced FVector::ForwardVector
    }

    if (GetVoxelAtWorld(WorldX, WorldY - 1, WorldZ) == EVoxelType::Air ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v000) < v000.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v001) < v001.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v101) < v101.Z ||
        GetNeighborTopHeightWorld(WorldX, WorldY - 1, WorldZ, v100) < v100.Z)
    {
        AddQuadWorld(v000, v001, v101, v100, FVector(-1.f, 0.f, 0.f), CurrentType); // Replaced FVector::BackwardVector
    }
}

void ASmoothVoxelTerrain::AppendGrassBladesWorld(int32 WorldX, int32 WorldY, int32 WorldZ, FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs)
{
    FDynamicMeshAttributeSet* Attr = Mesh.Attributes();
    if (!Attr) return;

    FDynamicMeshUVOverlay* UVOverlay0 = Attr->GetUVLayer(0);
    if (!UVOverlay0) return;

    if (Attr->NumUVLayers() < 2)
    {
        Attr->SetNumUVLayers(2);
    }
    FDynamicMeshUVOverlay* UVOverlay1 = Attr->GetUVLayer(1);

    if (!Attr->PrimaryColors())
    {
        Attr->EnablePrimaryColors();
    }
    FDynamicMeshColorOverlay* ColorOverlay = Attr->PrimaryColors();

    float DensityNoise = FMath::PerlinNoise2D(FVector2D((float)WorldX, (float)WorldY) * GrassDensityNoiseScale);
    DensityNoise = FMath::Clamp((DensityNoise + 1.0f) * 0.5f, 0.0f, 1.0f);
    int32 Density = FMath::RoundToInt(FMath::Lerp((float)GrassMinDensity, (float)GrassMaxDensity, DensityNoise));

    float ColorNoise = FMath::PerlinNoise2D(FVector2D((float)WorldX, (float)WorldY) * GrassColorNoiseScale);
    ColorNoise = FMath::Clamp((ColorNoise + 1.0f) * 0.5f, 0.0f, 1.0f);

    FLinearColor BaseColor = FLinearColor::LerpUsingHSV(GrassBaseColorDark, GrassBaseColorLight, ColorNoise);
    FLinearColor TipColor = FLinearColor::LerpUsingHSV(GrassTipColorDark, GrassTipColorLight, ColorNoise);

    auto Hash2D = [](float x, float y) -> float
        {
            return FMath::Fractional(FMath::Sin(x * 12.9898f + y * 78.233f) * 43758.5453f);
        };

    for (int32 i = 0; i < Density; ++i)
    {
        float R1 = Hash2D((float)WorldX + i * 0.17f, (float)WorldY + i * 0.43f);
        float R2 = Hash2D((float)WorldX - i * 0.31f, (float)WorldY + i * 0.79f);
        float R3 = Hash2D((float)WorldX + i * 0.59f, (float)WorldY - i * 0.23f);

        float dx = 0.15f + 0.7f * R1;
        float dy = 0.15f + 0.7f * R2;

        float BladeWorldX = (float)WorldX + dx;
        float BladeWorldY = (float)WorldY + dy;
        float BladeWorldZ = 0.0f;
        FVector GroundNormal(0.f, 0.f, 1.f); // Replaced FVector::UpVector

        if (bSmoothTerrain)
        {
            BladeWorldZ = GetInterpolatedHeight(BladeWorldX, BladeWorldY) * CubeSize;
            GroundNormal = GetSmoothNormalWorld(WorldX, WorldY);
        }
        else
        {
            BladeWorldZ = (float)(WorldZ + 1) * CubeSize;
        }

        FVector BasePos(BladeWorldX * CubeSize, BladeWorldY * CubeSize, BladeWorldZ);

        float Height = GrassMinHeight + (GrassMaxHeight - GrassMinHeight) * R2;
        float Width = GrassMinWidth + (GrassMaxWidth - GrassMinWidth) * R1;

        float Angle = R3 * 2.0f * PI;
        FVector BladeRight(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
        FVector BladeForward(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f);

        float BladeTint = (R3 * 0.2f) - 0.1f;
        FLinearColor CustomBaseColor = FLinearColor::LerpUsingHSV(BaseColor, FLinearColor::Black, FMath::Max(0.0f, -BladeTint));
        CustomBaseColor = FLinearColor::LerpUsingHSV(CustomBaseColor, FLinearColor::White, FMath::Max(0.0f, BladeTint));
        FLinearColor CustomTipColor = FLinearColor::LerpUsingHSV(TipColor, FLinearColor::Black, FMath::Max(0.0f, -BladeTint));
        CustomTipColor = FLinearColor::LerpUsingHSV(CustomTipColor, FLinearColor::White, FMath::Max(0.0f, BladeTint));
        FLinearColor MidColor = FLinearColor::LerpUsingHSV(CustomBaseColor, CustomTipColor, 0.5f);

        float BendForce = (0.2f + 0.3f * R3) * Height;
        FVector BendDir = (BladeForward + GroundNormal * 0.2f).GetSafeNormal();

        FVector V0 = BasePos - BladeRight * (Width * 0.5f);
        FVector V1 = BasePos + BladeRight * (Width * 0.5f);
        FVector V2 = BasePos - BladeRight * (Width * 0.3f) + BendDir * (BendForce * 0.35f) + GroundNormal * (Height * 0.5f);
        FVector V3 = BasePos + BladeRight * (Width * 0.3f) + BendDir * (BendForce * 0.35f) + GroundNormal * (Height * 0.5f);
        FVector V4 = BasePos + BendDir * BendForce + GroundNormal * Height;

        int32 v0 = Mesh.AppendVertex(FVector3d(V0));
        int32 v1 = Mesh.AppendVertex(FVector3d(V1));
        int32 v2 = Mesh.AppendVertex(FVector3d(V2));
        int32 v3 = Mesh.AppendVertex(FVector3d(V3));
        int32 v4 = Mesh.AppendVertex(FVector3d(V4));

        FDynamicMeshNormalOverlay* NormalOverlay = Attr->PrimaryNormals();
        int32 nGround = NormalOverlay ? NormalOverlay->AppendElement(FVector3f(GroundNormal)) : -1;

        int32 uv0_0 = UVOverlay0->AppendElement(FVector2f(0.0f, 0.0f));
        int32 uv0_1 = UVOverlay0->AppendElement(FVector2f(1.0f, 0.0f));
        int32 uv0_2 = UVOverlay0->AppendElement(FVector2f(0.15f, 0.5f));
        int32 uv0_3 = UVOverlay0->AppendElement(FVector2f(0.85f, 0.5f));
        int32 uv0_4 = UVOverlay0->AppendElement(FVector2f(0.5f, 1.0f));

        int32 uv1_0 = UVOverlay1 ? UVOverlay1->AppendElement(FVector2f(ColorNoise, 0.0f)) : -1;
        int32 uv1_1 = UVOverlay1 ? UVOverlay1->AppendElement(FVector2f(ColorNoise, 0.0f)) : -1;
        int32 uv1_2 = UVOverlay1 ? UVOverlay1->AppendElement(FVector2f(ColorNoise, 0.5f)) : -1;
        int32 uv1_3 = UVOverlay1 ? UVOverlay1->AppendElement(FVector2f(ColorNoise, 0.5f)) : -1;
        int32 uv1_4 = UVOverlay1 ? UVOverlay1->AppendElement(FVector2f(ColorNoise, 1.0f)) : -1;

        int32 c0 = ColorOverlay ? ColorOverlay->AppendElement(FVector4f(CustomBaseColor)) : -1;
        int32 c1 = ColorOverlay ? ColorOverlay->AppendElement(FVector4f(CustomBaseColor)) : -1;
        int32 c2 = ColorOverlay ? ColorOverlay->AppendElement(FVector4f(MidColor)) : -1;
        int32 c3 = ColorOverlay ? ColorOverlay->AppendElement(FVector4f(MidColor)) : -1;
        int32 c4 = ColorOverlay ? ColorOverlay->AppendElement(FVector4f(CustomTipColor)) : -1;

        auto AddTri = [UVOverlay0, UVOverlay1, ColorOverlay, NormalOverlay, nGround, &Mesh, &OutTriIDs](
            int32 a, int32 b, int32 c,
            int32 u0_A, int32 u0_B, int32 u0_C,
            int32 u1_A, int32 u1_B, int32 u1_C,
            int32 cA, int32 cB, int32 cC)
            {
                int32 t = Mesh.AppendTriangle(a, b, c);
                if (t != FDynamicMesh3::InvalidID)
                {
                    OutTriIDs.Add(t);
                    if (NormalOverlay && nGround != -1 && Mesh.IsTriangle(t))
                    {
                        NormalOverlay->SetTriangle(t, FIndex3i(nGround, nGround, nGround));
                    }
                    if (UVOverlay0 && u0_A != -1 && u0_B != -1 && u0_C != -1 && Mesh.IsTriangle(t))
                    {
                        UVOverlay0->SetTriangle(t, FIndex3i(u0_A, u0_B, u0_C));
                    }
                    if (UVOverlay1 && u1_A != -1 && u1_B != -1 && u1_C != -1 && Mesh.IsTriangle(t))
                    {
                        UVOverlay1->SetTriangle(t, FIndex3i(u1_A, u1_B, u1_C));
                    }
                    if (ColorOverlay && cA != -1 && cB != -1 && cC != -1 && Mesh.IsTriangle(t))
                    {
                        ColorOverlay->SetTriangle(t, FIndex3i(cA, cB, cC));
                    }
                }

                int32 tBack = Mesh.AppendTriangle(a, c, b);
                if (tBack != FDynamicMesh3::InvalidID)
                {
                    OutTriIDs.Add(tBack);
                    if (NormalOverlay && nGround != -1 && Mesh.IsTriangle(tBack))
                    {
                        NormalOverlay->SetTriangle(tBack, FIndex3i(nGround, nGround, nGround));
                    }
                    if (UVOverlay0 && u0_A != -1 && u0_B != -1 && u0_C != -1 && Mesh.IsTriangle(tBack))
                    {
                        UVOverlay0->SetTriangle(tBack, FIndex3i(u0_A, u0_C, u0_B));
                    }
                    if (UVOverlay1 && u1_A != -1 && u1_B != -1 && u1_C != -1 && Mesh.IsTriangle(tBack))
                    {
                        UVOverlay1->SetTriangle(tBack, FIndex3i(u1_A, u1_C, u1_B));
                    }
                    if (ColorOverlay && cA != -1 && cB != -1 && cC != -1 && Mesh.IsTriangle(tBack))
                    {
                        ColorOverlay->SetTriangle(tBack, FIndex3i(cA, cC, cB));
                    }
                }
            };

        AddTri(v0, v1, v3, uv0_0, uv0_1, uv0_3, uv1_0, uv1_1, uv1_3, c0, c1, c3);
        AddTri(v0, v3, v2, uv0_0, uv0_3, uv0_2, uv1_0, uv1_3, uv1_2, c0, c3, c2);
        AddTri(v2, v3, v4, uv0_2, uv0_3, uv0_4, uv1_2, uv1_3, uv1_4, c2, c3, c4);
    }
}

void ASmoothVoxelTerrain::FVoxelChunk::UpdateVoxelMesh(int32 LocalX, int32 LocalY, int32 LocalZ, EVoxelType NewType, ASmoothVoxelTerrain* TerrainOwner)
{
    if (!MeshComponent || !GrassMeshComponent) return;

    UDynamicMesh* DynamicMesh = MeshComponent->GetDynamicMesh();
    UDynamicMesh* GrassDynamicMesh = GrassMeshComponent->GetDynamicMesh();
    if (!DynamicMesh || !GrassDynamicMesh) return;

    DynamicMesh->EditMesh([&](FDynamicMesh3& MeshOut)
        {
            GrassDynamicMesh->EditMesh([&](FDynamicMesh3& GrassMeshOut)
                {
                    FDynamicMeshAttributeSet* Attr = MeshOut.Attributes();
                    FDynamicMeshAttributeSet* GrassAttr = GrassMeshOut.Attributes();
                    if (!Attr || !GrassAttr) return;

                    if (Attr->NumUVLayers() < 2) Attr->SetNumUVLayers(2);
                    if (GrassAttr->NumUVLayers() < 2) GrassAttr->SetNumUVLayers(2);

                    if (!Attr->PrimaryColors()) Attr->EnablePrimaryColors();
                    if (!GrassAttr->PrimaryColors()) GrassAttr->EnablePrimaryColors();

                    for (int32 dz = -1; dz <= 1; ++dz)
                    {
                        for (int32 dy = -1; dy <= 1; ++dy)
                        {
                            for (int32 dx = -1; dx <= 1; ++dx)
                            {
                                int32 dist = FMath::Abs(dx) + FMath::Abs(dy) + FMath::Abs(dz);
                                if (dist != 0 && dist != 1) continue;

                                int32 nx = LocalX + dx;
                                int32 ny = LocalY + dy;
                                int32 nz = LocalZ + dz;
                                if (nx >= 0 && nx < TerrainOwner->ChunkSize &&
                                    ny >= 0 && ny < TerrainOwner->ChunkSize &&
                                    nz >= 0 && nz < TerrainOwner->MaxHeight)
                                {
                                    RemoveVoxelFaces(nx, ny, nz, MeshOut, GrassMeshOut, TerrainOwner);
                                }
                            }
                        }
                    }

                    int32 Index = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
                    VoxelData[Index] = NewType;

                    for (int32 dz = -1; dz <= 1; ++dz)
                    {
                        for (int32 dy = -1; dy <= 1; ++dy)
                        {
                            for (int32 dx = -1; dx <= 1; ++dx)
                            {
                                int32 dist = FMath::Abs(dx) + FMath::Abs(dy) + FMath::Abs(dz);
                                if (dist != 0 && dist != 1) continue;

                                int32 nx = LocalX + dx;
                                int32 ny = LocalY + dy;
                                int32 nz = LocalZ + dz;
                                if (nx >= 0 && nx < TerrainOwner->ChunkSize &&
                                    ny >= 0 && ny < TerrainOwner->ChunkSize &&
                                    nz >= 0 && nz < TerrainOwner->MaxHeight)
                                {
                                    int32 neighborIndex = nx + ny * TerrainOwner->ChunkSize + nz * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;
                                    if (VoxelData[neighborIndex] != EVoxelType::Air)
                                    {
                                        AddVoxelFaces(nx, ny, nz, MeshOut, GrassMeshOut, TerrainOwner);
                                    }
                                }
                            }
                        }
                    }
                });
        });
}

void ASmoothVoxelTerrain::FVoxelChunk::RemoveVoxelFaces(int32 LocalX, int32 LocalY, int32 LocalZ, FDynamicMesh3& Mesh, FDynamicMesh3& GrassMesh, ASmoothVoxelTerrain* TerrainOwner)
{
    int32 VoxelIndex = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;

    TArray<int32>& TriIDs = VoxelTriangles[VoxelIndex];
    for (int32 TriID : TriIDs)
    {
        if (Mesh.IsTriangle(TriID))
        {
            Mesh.RemoveTriangle(TriID, false);
        }
    }
    TriIDs.Empty();

    TArray<int32>& GrassTriIDs = GrassVoxelTriangles[VoxelIndex];
    for (int32 TriID : GrassTriIDs)
    {
        if (GrassMesh.IsTriangle(TriID))
        {
            GrassMesh.RemoveTriangle(TriID, false);
        }
    }
    GrassTriIDs.Empty();
}

void ASmoothVoxelTerrain::FVoxelChunk::AddVoxelFaces(int32 LocalX, int32 LocalY, int32 LocalZ, FDynamicMesh3& Mesh, FDynamicMesh3& GrassMesh, ASmoothVoxelTerrain* TerrainOwner)
{
    int32 WorldX = Coord.X * TerrainOwner->ChunkSize + LocalX;
    int32 WorldY = Coord.Y * TerrainOwner->ChunkSize + LocalY;
    int32 WorldZ = LocalZ;

    int32 VoxelIndex = LocalX + LocalY * TerrainOwner->ChunkSize + LocalZ * TerrainOwner->ChunkSize * TerrainOwner->ChunkSize;

    TArray<int32> NewTriIDs;
    TerrainOwner->AppendVoxelFacesWorld(WorldX, WorldY, WorldZ, Mesh, NewTriIDs);
    VoxelTriangles[VoxelIndex] = MoveTemp(NewTriIDs);

    TArray<int32> NewGrassTriIDs;
    if (TerrainOwner->bEnableGrassGeometry && VoxelData[VoxelIndex] == EVoxelType::Grass)
    {
        if (TerrainOwner->GetVoxelAtWorld(WorldX, WorldY, WorldZ + 1) == EVoxelType::Air)
        {
            TerrainOwner->AppendGrassBladesWorld(WorldX, WorldY, WorldZ, GrassMesh, NewGrassTriIDs);
        }
    }
    GrassVoxelTriangles[VoxelIndex] = MoveTemp(NewGrassTriIDs);
}

ASmoothVoxelTerrain::FVoxelChunk* ASmoothVoxelTerrain::GetChunk(const FIntVector& Coord)
{
    if (auto* Ptr = Chunks.Find(Coord))
    {
        return Ptr->IsValid() ? Ptr->Get() : nullptr;
    }
    return nullptr;
}

const ASmoothVoxelTerrain::FVoxelChunk* ASmoothVoxelTerrain::GetChunk(const FIntVector& Coord) const
{
    if (auto* Ptr = Chunks.Find(Coord))
    {
        return Ptr->IsValid() ? Ptr->Get() : nullptr;
    }
    return nullptr;
}

#if WITH_EDITOR
void ASmoothVoxelTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

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
            if (Pair.Value->GrassMeshComponent)
            {
                Pair.Value->GrassMeshComponent->SetReceivesDecals(bReceivesDecals);
            }
        }
    }
    // Removed redundant RebuildTerrain() trigger that conflicted with editor's OnConstruction cycle
}
#endif