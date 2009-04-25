//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// a Tile represents a single field on the map.
// it is a list of Items
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////

#ifndef __OTSERV_TILE_H__
#define __OTSERV_TILE_H__
#include "cylinder.h"
#include "item.h"

class Creature;
class Teleport;
class TrashHolder;
class Mailbox;
class MagicField;
class QTreeLeafNode;
class BedItem;

typedef std::vector<Item*> ItemVector;
typedef std::vector<Creature*> CreatureVector;

enum tileflags_t
{
	TILESTATE_NONE = 0,
	TILESTATE_PROTECTIONZONE = 1 << 0,
	TILESTATE_TRASHED = 1 << 1,
	TILESTATE_NOPVPZONE = 1 << 2,
	TILESTATE_NOLOGOUT = 1 << 3,
	TILESTATE_PVPZONE = 1 << 4,
	TILESTATE_REFRESH = 1 << 5,

	//internal usage
	TILESTATE_HOUSE = 1 << 6,
	TILESTATE_FLOORCHANGE = 1 << 7,
	TILESTATE_FLOORCHANGE_DOWN = 1 << 8,
	TILESTATE_FLOORCHANGE_NORTH = 1 << 9,
	TILESTATE_FLOORCHANGE_SOUTH = 1 << 10,
	TILESTATE_FLOORCHANGE_EAST = 1 << 11,
	TILESTATE_FLOORCHANGE_WEST = 1 << 12,
	TILESTATE_POSITIONCHANGE = 1 << 13,
	TILESTATE_MAGICFIELD = 1 << 14,
	TILESTATE_BLOCKSOLID = 1 << 15,
	TILESTATE_BLOCKPATH = 1 << 16,
	TILESTATE_IMMOVABLEBLOCKSOLID = 1 << 17,
	TILESTATE_IMMOVABLEBLOCKPATH = 1 << 18,
	TILESTATE_IMMOVABLENOFIELDBLOCKPATH = 1 << 19,
	TILESTATE_NOFIELDBLOCKPATH = 1 << 20
};

class Tile : public Cylinder
{
	public:
		static Tile nullTile;
		Tile(uint16_t x, uint16_t y, uint16_t z)
		{
			tilePos = Position(x, y, z);
			m_flags = thingCount = 0;
			qt_node = NULL;
			ground = NULL;
			downItems = NULL;
			topItems = NULL;
			creatures = NULL;
		}

		virtual ~Tile()
		{
			delete ground;
			if(topItems)
			{
				for(ItemVector::iterator it = topItems->begin(); it != topItems->end(); ++it)
					delete *it;
			}

			topItems->clear();
			if(downItems)
			{
				for(ItemVector::iterator it = downItems->begin(); it != downItems->end(); ++it)
					delete *it;
			}

			downItems->clear();
		}

		virtual int32_t getThrowRange() const {return 0;}
		virtual bool isPushable() const {return false;}

		Item* ground;
		QTreeLeafNode* qt_node;
		ItemVector* downItems;
		ItemVector* topItems;
		CreatureVector* creatures;

		MagicField* getFieldItem() const;
		Teleport* getTeleportItem() const;
		TrashHolder* getTrashHolder() const;
		Mailbox* getMailbox() const;
		BedItem* getBedItem() const;

		Creature* getTopCreature();
		Item* getTopTopItem();
		Item* getTopDownItem();
		bool isMoveableBlocking() const;
		Thing* getTopThing();
		Item* getItemByTopOrder(int32_t topOrder);

		bool hasProperty(enum ITEMPROPERTY prop) const;
		bool hasProperty(Item* exclude, enum ITEMPROPERTY prop) const;

		bool hasFlag(tileflags_t flag) const {return ((m_flags & (uint32_t)flag) == (uint32_t)flag);}
		void setFlag(tileflags_t flag) {m_flags |= (uint32_t)flag;}
		void resetFlag(tileflags_t flag) {m_flags &= ~(uint32_t)flag;}

		bool floorChange() const {return hasFlag(TILESTATE_FLOORCHANGE);}
		bool floorChangeDown() const {return hasFlag(TILESTATE_FLOORCHANGE_DOWN);}
		bool positionChange() const {return hasFlag(TILESTATE_POSITIONCHANGE);}
		bool floorChange(Direction direction) const
		{
			switch(direction)
			{
				case NORTH:
					return hasFlag(TILESTATE_FLOORCHANGE_NORTH);
				case SOUTH:
					return hasFlag(TILESTATE_FLOORCHANGE_SOUTH);
				case EAST:
					return hasFlag(TILESTATE_FLOORCHANGE_EAST);
				case WEST:
					return hasFlag(TILESTATE_FLOORCHANGE_WEST);
				default:
					break;
			}

			return false;
		}

		bool hasHeight(uint32_t n) const;
		uint32_t getHeight() const;

		virtual std::string getDescription(int32_t lookDistance) const;
		void moveCreature(Creature* creature, Cylinder* toCylinder, bool teleport = false);
		uint32_t getThingCount() const {return thingCount;}

		//cylinder implementations
		virtual ReturnValue __queryAdd(int32_t index, const Thing* thing, uint32_t count,
			uint32_t flags) const;
		virtual ReturnValue __queryMaxCount(int32_t index, const Thing* thing, uint32_t count,
			uint32_t& maxQueryCount, uint32_t flags) const;
		virtual ReturnValue __queryRemove(const Thing* thing, uint32_t count, uint32_t flags) const;
		virtual Cylinder* __queryDestination(int32_t& index, const Thing* thing, Item** destItem,
			uint32_t& flags);

		virtual void __addThing(Creature* actor, Thing* thing);
		virtual void __addThing(Creature* actor, int32_t index, Thing* thing);

		virtual void __updateThing(Thing* thing, uint16_t itemId, uint32_t count);
		virtual void __replaceThing(uint32_t index, Thing* thing);

		virtual void __removeThing(Thing* thing, uint32_t count);

		virtual int32_t __getIndexOfThing(const Thing* thing) const;
		virtual int32_t __getFirstIndex() const;
		virtual int32_t __getLastIndex() const;
		virtual uint32_t __getItemTypeCount(uint16_t itemId, int32_t subType = -1, bool itemCount = true) const;
		virtual Thing* __getThing(uint32_t index) const;

		virtual void postAddNotification(Creature* actor, Thing* thing, int32_t index, cylinderlink_t link = LINK_OWNER);
		virtual void postRemoveNotification(Creature* actor, Thing* thing, int32_t index, bool isCompleteRemoval, cylinderlink_t link = LINK_OWNER);

		virtual void __internalAddThing(Thing* thing);
		virtual void __internalAddThing(uint32_t index, Thing* thing);

		virtual const Position& getPosition() const {return tilePos;}
		const Position& getTilePosition() const {return tilePos;}

		virtual bool isHouseTile() const {return false;}
		virtual bool isRemoved() const {return false;}

	private:
		void onAddTileItem(Item* item);
		void onUpdateTileItem(uint32_t index, Item* oldItem,
			const ItemType& oldType, Item* newItem, const ItemType& newType);
		void onRemoveTileItem(uint32_t index, Item* item);

		void onUpdateTile();
		void updateTileFlags(Item* item, bool removing);

	protected:
		Position tilePos;
		uint32_t m_flags;
		uint32_t thingCount;
};

#endif
