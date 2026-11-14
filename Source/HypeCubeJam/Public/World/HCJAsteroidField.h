#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJMatchConfig.h"
#include "World/HCJSectorLayout.h"

/**
 * Destroyable asteroids: plain logic without a world (tested on its own). AHCJUniverse holds the instances,
 * AHCJGameState replicates which rocks are gone.
 *
 * MODEL: the sector layouts never change (generator, checksum and golden tests untouched). On top of them every
 * rock is a NODE with a 16-bit id: a rock of the layout is its index; a rock that is big enough (FHCJAsteroidRules)
 * breaks into two or three pieces, which are nodes of their own (the parent's id plus a slot number), and a piece
 * that is still big enough breaks once more - two generations at most. What the pieces of a node are is a pure
 * function of (sector seed, node id, the node's own shape): MakeChildren. So the whole state of a match is the SET
 * OF DESTROYED NODE IDS: a node exists when all its ancestors are in the set and it is not. The server owns that
 * set and the health of the rocks; every machine derives the same pieces from it, in any order of arrival.
 *
 * Pieces lie inside the rock they come from (inside its sphere / its box, same rotation), so every clearance the
 * generators guarantee around a rock (spawn points, pads, portals, wormholes, stations) holds for its pieces too.
 */
struct FHCJDestroyedSet;

namespace HCJAsteroids
{
	/** A layout rock index must fit into 12 bits (a sector has at most a few hundred). */
	inline constexpr int32 MaxBaseIndex = 4095;
	inline constexpr int32 MaxChildren = 3;
	inline constexpr int32 MaxGeneration = 2;
	/** The server stops destroying rocks at this many per match (far above anything a match reaches; keeps the replicated list bounded). */
	inline constexpr int32 MaxDestroyedPerMatch = 1800;

	/** Node id: bits 0..11 the layout index, bits 12..13 the slot (1..3) of the first break, bits 14..15 of the second; 0 = none. */
	inline uint16 MakeId(int32 BaseIndex, int32 Slot1 = 0, int32 Slot2 = 0)
	{
		return static_cast<uint16>((BaseIndex & 0x0FFF) | ((Slot1 & 3) << 12) | ((Slot2 & 3) << 14));
	}
	inline int32 GetBaseIndex(uint16 Id) { return Id & 0x0FFF; }
	inline int32 GetSlot(uint16 Id, int32 Generation) { return Generation == 1 ? (Id >> 12) & 3 : Generation == 2 ? (Id >> 14) & 3 : 0; }
	/** 0 for a rock of the layout, 1 for a piece, 2 for a piece of a piece. */
	inline int32 GetGeneration(uint16 Id) { return GetSlot(Id, 2) != 0 ? 2 : GetSlot(Id, 1) != 0 ? 1 : 0; }
	/** Well formed: a second slot only with a first one. */
	inline bool IsValidId(uint16 Id) { return GetSlot(Id, 2) == 0 || GetSlot(Id, 1) != 0; }
	/** Id of piece Slot (1..MaxChildren) of ParentId; ParentId must be of generation 0 or 1. */
	inline uint16 MakeChildId(uint16 ParentId, int32 Slot)
	{
		return GetGeneration(ParentId) == 0 ? MakeId(GetBaseIndex(ParentId), Slot) : MakeId(GetBaseIndex(ParentId), GetSlot(ParentId, 1), Slot);
	}
	inline uint16 GetParentId(uint16 Id)
	{
		return GetGeneration(Id) == 2 ? MakeId(GetBaseIndex(Id), GetSlot(Id, 1)) : MakeId(GetBaseIndex(Id));
	}

	/** What is replicated per destroyed rock: sector in the high word, node id in the low word. */
	inline uint32 PackKey(int32 Sector, uint16 Id) { return (static_cast<uint32>(Sector) << 16) | Id; }
	inline int32 GetKeySector(uint32 Key) { return static_cast<int32>(Key >> 16); }
	inline uint16 GetKeyId(uint32 Key) { return static_cast<uint16>(Key & 0xFFFF); }

	/** A rock of this size and generation breaks into pieces (otherwise it is simply gone). */
	HYPECUBEJAM_API bool CanSplit(float BoundingRadius, int32 Generation, const FHCJAsteroidRules& Rules);

	/** Hit points of a rock of this size. */
	HYPECUBEJAM_API float ComputeHealth(float BoundingRadius, const FHCJAsteroidRules& Rules);

	/**
	 * The pieces of node ParentId (whose shape is Parent) in the sector with seed SectorSeed: two or three, slot 1
	 * first, in sector-local space like the parent. Empty when the node does not split. Pure; only + - * / and sqrt
	 * on numbers from HCJ::FFeatureHash, so every machine gets the same bits.
	 */
	HYPECUBEJAM_API void MakeChildren(int32 SectorSeed, uint16 ParentId, const FHCJAsteroid& Parent, const FHCJAsteroidRules& Rules, TArray<FHCJAsteroid>& OutChildren);

	/** The shape of node Id of a sector: walks from the layout rock through the breaks. False for an id that names nothing. */
	HYPECUBEJAM_API bool ResolveNode(const FHCJSectorLayout& Layout, uint16 Id, const FHCJAsteroidRules& Rules, FHCJAsteroid& OutAsteroid);

	/**
	 * What comes to exist in sector Sector when node ParentId (shape Parent) is removed, given everything else that
	 * is destroyed: its pieces - and, in place of a piece that is itself already destroyed (its key arrived first),
	 * that piece's pieces. Appends (id, shape) pairs to OutPieces. This is what makes the result independent of the
	 * order in which the destroyed keys arrive.
	 */
	HYPECUBEJAM_API void CollectPieces(int32 Sector, int32 SectorSeed, uint16 ParentId, const FHCJAsteroid& Parent, const FHCJAsteroidRules& Rules,
		const FHCJDestroyedSet& Destroyed, TArray<TPair<uint16, FHCJAsteroid>>& OutPieces);

	/** CRC32 of the sorted keys: equal on two machines = the same rocks are gone (order of arrival does not matter). 0 for none. */
	HYPECUBEJAM_API uint32 ComputeDestroyedChecksum(TConstArrayView<uint32> Keys);
}

/**
 * Which nodes are destroyed, and from that which exist. One per universe (all sectors). Plain data; the same class
 * on the server and on clients, fed in whatever order the keys arrive.
 */
struct HYPECUBEJAM_API FHCJDestroyedSet
{
	/** Marks a node destroyed; false when it already was. */
	bool Add(uint32 Key);
	bool Contains(uint32 Key) const { return Keys.Contains(Key); }
	int32 Num() const { return Keys.Num(); }
	void Reset() { Keys.Reset(); }
	/**
	 * The node exists right now: it is not destroyed and all its ancestors are (a rock of the layout has none).
	 * Says nothing about whether the id names a real piece (ResolveNode does).
	 */
	bool IsAlive(uint32 Key) const;
	/** Keys in no particular order. */
	TArray<uint32> ToArray() const { return Keys.Array(); }
	uint32 ComputeChecksum() const { return HCJAsteroids::ComputeDestroyedChecksum(Keys.Array()); }

private:
	TSet<uint32> Keys;
};
