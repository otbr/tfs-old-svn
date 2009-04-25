//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
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
#include "otpch.h"

#include "game.h"
#include "creature.h"
#include "player.h"
#include "tile.h"
#include "tools.h"
#include "combat.h"
#include "vocation.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "movement.h"

extern Game g_game;
extern MoveEvents* g_moveEvents;

MoveEvents::MoveEvents() :
m_scriptInterface("MoveEvents Interface")
{
	m_scriptInterface.initState();
}

MoveEvents::~MoveEvents()
{
	clear();
}

inline void MoveEvents::clearMap(MoveListMap& map)
{
	for(MoveListMap::iterator it = map.begin(); it != map.end(); ++it)
	{
		for(int32_t i = MOVE_EVENT_FIRST; i <= MOVE_EVENT_LAST; ++i)
		{
			EventList& moveEventList = it->second.moveEvent[i];
			for(EventList::iterator it = moveEventList.begin(); it != moveEventList.end(); ++it)
				delete (*it);
		}
	}

	map.clear();
}

void MoveEvents::clear()
{
	clearMap(m_itemIdMap);
	clearMap(m_actionIdMap);
	clearMap(m_uniqueIdMap);
	for(MovePosListMap::iterator it = m_positionMap.begin(); it != m_positionMap.end(); ++it)
	{
		for(int32_t i = MOVE_EVENT_FIRST; i <= MOVE_EVENT_LAST; ++i)
		{
			EventList& moveEventList = it->second.moveEvent[i];
			for(EventList::iterator it = moveEventList.begin(); it != moveEventList.end(); ++it)
				delete (*it);
		}
	}

	m_positionMap.clear();
	m_scriptInterface.reInitState();
}

Event* MoveEvents::getEvent(const std::string& nodeName)
{
	std::string tmpNodeName = asLowerCaseString(nodeName);
	if(tmpNodeName == "movevent" || tmpNodeName == "moveevent" || tmpNodeName == "movement")
		return new MoveEvent(&m_scriptInterface);

	return NULL;
}

bool MoveEvents::registerEvent(Event* event, xmlNodePtr p)
{
	MoveEvent* moveEvent = dynamic_cast<MoveEvent*>(event);
	if(!moveEvent)
		return false;

	std::string strValue, endStrValue;
	MoveEvent_t eventType = moveEvent->getEventType();
	if(eventType == MOVE_EVENT_ADD_ITEM || eventType == MOVE_EVENT_REMOVE_ITEM)
	{
		if(readXMLString(p, "tileitem", strValue) && booleanString(strValue))
		{
			switch(eventType)
			{
				case MOVE_EVENT_ADD_ITEM:
					moveEvent->setEventType(MOVE_EVENT_ADD_ITEM_ITEMTILE);
					break;
				case MOVE_EVENT_REMOVE_ITEM:
					moveEvent->setEventType(MOVE_EVENT_REMOVE_ITEM_ITEMTILE);
					break;
				default:
					break;
			}
		}
	}

	StringVec strVector;
	IntegerVec intVector;
	IntegerVec endIntVector;

	bool success = true;
	if(readXMLString(p, "itemid", strValue))
	{
		strVector = explodeString(strValue, ";");
		for(StringVec::iterator it = strVector.begin(); it != strVector.end(); ++it)
		{
			intVector = vectorAtoi(explodeString((*it), "-"));
			if(!intVector[0])
				continue;

			bool equip = moveEvent->getEventType() == MOVE_EVENT_EQUIP;
			addEvent(moveEvent, intVector[0], m_itemIdMap);
			if(equip)
			{
				ItemType& it = Item::items.getItemType(intVector[0]);
				it.wieldInfo = moveEvent->getWieldInfo();
				it.minReqLevel = moveEvent->getReqLevel();
				it.minReqMagicLevel = moveEvent->getReqMagLv();
				it.vocationString = moveEvent->getVocationString();
			}

			if(intVector.size() > 1)
			{
				while(intVector[0] < intVector[1])
				{
					addEvent(new MoveEvent(moveEvent), ++intVector[0], m_itemIdMap);
					if(equip)
					{
						ItemType& tit = Item::items.getItemType(intVector[0]);
						tit.wieldInfo = moveEvent->getWieldInfo();
						tit.minReqLevel = moveEvent->getReqLevel();
						tit.minReqMagicLevel = moveEvent->getReqMagLv();
						tit.vocationString = moveEvent->getVocationString();
					}
				}
			}
		}
	}
	else if(readXMLString(p, "fromid", strValue) && readXMLString(p, "toid", endStrValue))
	{
		intVector = vectorAtoi(explodeString(strValue, ";"));
		endIntVector = vectorAtoi(explodeString(endStrValue, ";"));
		if(intVector[0] && endIntVector[0] && intVector.size() == endIntVector.size())
		{
			size_t size = intVector.size();
			for(size_t i = 0; i < size; ++i)
			{
				bool equip = moveEvent->getEventType() == MOVE_EVENT_EQUIP;
				addEvent(moveEvent, intVector[i], m_itemIdMap);
				if(equip)
				{
					ItemType& it = Item::items.getItemType(intVector[i]);
					it.wieldInfo = moveEvent->getWieldInfo();
					it.minReqLevel = moveEvent->getReqLevel();
					it.minReqMagicLevel = moveEvent->getReqMagLv();
					it.vocationString = moveEvent->getVocationString();
				}

				while(intVector[i] < endIntVector[i])
				{
					addEvent(new MoveEvent(moveEvent), ++intVector[i], m_itemIdMap);
					if(equip)
					{
						ItemType& tit = Item::items.getItemType(intVector[i]);
						tit.wieldInfo = moveEvent->getWieldInfo();
						tit.minReqLevel = moveEvent->getReqLevel();
						tit.minReqMagicLevel = moveEvent->getReqMagLv();
						tit.vocationString = moveEvent->getVocationString();
					}
				}
			}
		}
		else
			std::cout << "[Warning - MoveEvents::registerEvent] Malformed entry (from: \"" << strValue << "\", to: \"" << endStrValue << "\")" << std::endl;
	}
	else if(readXMLString(p, "uniqueid", strValue))
	{
		strVector = explodeString(strValue, ";");
		for(StringVec::iterator it = strVector.begin(); it != strVector.end(); ++it)
		{
			intVector = vectorAtoi(explodeString((*it), "-"));
			if(!intVector[0])
				continue;

			addEvent(moveEvent, intVector[0], m_uniqueIdMap);
			if(intVector.size() > 1)
			{
				while(intVector[0] < intVector[1])
					addEvent(new MoveEvent(moveEvent), ++intVector[0], m_uniqueIdMap);
			}
		}
	}
	else if(readXMLString(p, "fromuid", strValue) && readXMLString(p, "touid", endStrValue))
	{
		intVector = vectorAtoi(explodeString(strValue, ";"));
		endIntVector = vectorAtoi(explodeString(endStrValue, ";"));
		if(intVector[0] && endIntVector[0] && intVector.size() == endIntVector.size())
		{
			size_t size = intVector.size();
			for(size_t i = 0; i < size; ++i)
			{
				addEvent(moveEvent, intVector[i], m_uniqueIdMap);
				while(intVector[i] < endIntVector[i])
					addEvent(new MoveEvent(moveEvent), ++intVector[i], m_uniqueIdMap);
			}
		}
		else
			std::cout << "[Warning - MoveEvents::registerEvent] Malformed entry (from: \"" << strValue << "\", to: \"" << endStrValue << "\")" << std::endl;
	}
	else if(readXMLString(p, "actionid", strValue))
	{
		strVector = explodeString(strValue, ";");
		for(StringVec::iterator it = strVector.begin(); it != strVector.end(); ++it)
		{
			intVector = vectorAtoi(explodeString((*it), "-"));
			if(!intVector[0])
				continue;

			addEvent(moveEvent, intVector[0], m_actionIdMap);
			if(intVector.size() > 1)
			{
				while(intVector[0] < intVector[1])
					addEvent(new MoveEvent(moveEvent), ++intVector[0], m_actionIdMap);
			}
		}
	}
	else if(readXMLString(p, "fromaid", strValue) && readXMLString(p, "toaid", endStrValue))
	{
		intVector = vectorAtoi(explodeString(strValue, ";"));
		endIntVector = vectorAtoi(explodeString(endStrValue, ";"));
		if(intVector[0] && endIntVector[0] && intVector.size() == endIntVector.size())
		{
			size_t size = intVector.size();
			for(size_t i = 0; i < size; ++i)
			{
				addEvent(moveEvent, intVector[i], m_actionIdMap);
				while(intVector[i] < endIntVector[i])
					addEvent(new MoveEvent(moveEvent), ++intVector[i], m_actionIdMap);
			}
		}
		else
			std::cout << "[Warning - MoveEvents::registerEvent] Malformed entry (from: \"" << strValue << "\", to: \"" << endStrValue << "\")" << std::endl;
	}
	else if(readXMLString(p, "pos", strValue) || readXMLString(p, "pos", strValue))
	{
		strVector = explodeString(strValue, ";");
		for(StringVec::iterator it = strVector.begin(); it != strVector.end(); ++it)
		{
			intVector = vectorAtoi(explodeString((*it), ","));
			if(intVector.size() > 2)
				addEvent(moveEvent, Position(intVector[0], intVector[1], intVector[2]), m_positionMap);
			else
				success = false;
		}
	}
	else
		success = false;

	return success;
}

void MoveEvents::addEvent(MoveEvent* moveEvent, int32_t id, MoveListMap& map)
{
	MoveListMap::iterator it = map.find(id);
	if(it == map.end())
	{
		MoveEventList moveEventList;
		moveEventList.moveEvent[moveEvent->getEventType()].push_back(moveEvent);
		map[id] = moveEventList;
	}
	else
	{
		EventList& moveEventList = it->second.moveEvent[moveEvent->getEventType()];
		for(EventList::iterator it = moveEventList.begin(); it != moveEventList.end(); ++it)
		{
			if((*it)->getSlot() == moveEvent->getSlot())
				std::cout << "[Warning - MoveEvents::addEvent] Duplicate move event found: " << id << std::endl;
		}

		moveEventList.push_back(moveEvent);
	}
}

MoveEvent* MoveEvents::getEvent(Item* item, MoveEvent_t eventType)
{
	MoveListMap::iterator it;
	if(item->getUniqueId() != 0)
	{
		it = m_uniqueIdMap.find(item->getUniqueId());
		if(it != m_uniqueIdMap.end())
		{
			EventList& moveEventList = it->second.moveEvent[eventType];
			if(!moveEventList.empty())
				return *moveEventList.begin();
		}
	}

	if(item->getActionId() != 0)
	{
		it = m_actionIdMap.find(item->getActionId());
		if(it != m_actionIdMap.end())
		{
			EventList& moveEventList = it->second.moveEvent[eventType];
			if(!moveEventList.empty())
				return *moveEventList.begin();
		}
	}

	it = m_itemIdMap.find(item->getID());
	if(it != m_itemIdMap.end())
	{
		EventList& moveEventList = it->second.moveEvent[eventType];
		if(!moveEventList.empty())
			return *moveEventList.begin();
	}

	return NULL;
}

MoveEvent* MoveEvents::getEvent(Item* item, MoveEvent_t eventType, slots_t slot)
{
	uint32_t slotp = 0;
	switch(slot)
	{
		case SLOT_HEAD:
			slotp = SLOTP_HEAD;
			break;
		case SLOT_NECKLACE:
			slotp = SLOTP_NECKLACE;
			break;
		case SLOT_BACKPACK:
			slotp = SLOTP_BACKPACK;
			break;
		case SLOT_ARMOR:
			slotp = SLOTP_ARMOR;
			break;
		case SLOT_RIGHT:
			slotp = SLOTP_RIGHT;
			break;
		case SLOT_LEFT:
			slotp = SLOTP_LEFT;
			break;
		case SLOT_LEGS:
			slotp = SLOTP_LEGS;
			break;
		case SLOT_FEET:
			slotp = SLOTP_FEET;
			break;
		case SLOT_AMMO:
			slotp = SLOTP_AMMO;
			break;
		case SLOT_RING:
			slotp = SLOTP_RING;
			break;
		default:
			break;
	}

	MoveListMap::iterator it = m_itemIdMap.find(item->getID());
	if(it != m_itemIdMap.end())
	{
		EventList& moveEventList = it->second.moveEvent[eventType];
		for(EventList::iterator it = moveEventList.begin(); it != moveEventList.end(); ++it)
		{
			if(((*it)->getSlot() & slotp) != 0)
				return *it;
		}
	}

	return NULL;
}

void MoveEvents::addEvent(MoveEvent* moveEvent, Position pos, MovePosListMap& map)
{
	MovePosListMap::iterator it = map.find(pos);
	if(it == map.end())
	{
		MoveEventList moveEventList;
		moveEventList.moveEvent[moveEvent->getEventType()].push_back(moveEvent);
		map[pos] = moveEventList;
	}
	else
	{
		EventList& moveEventList = it->second.moveEvent[moveEvent->getEventType()];
		if(!moveEventList.empty())
			std::cout << "[Warning - MoveEvents::addEvent] Duplicate move event found: " << pos << std::endl;

		moveEventList.push_back(moveEvent);
	}
}

MoveEvent* MoveEvents::getEvent(Tile* tile, MoveEvent_t eventType)
{
	MovePosListMap::iterator it = m_positionMap.find(tile->getPosition());
	if(it != m_positionMap.end())
	{
		EventList& moveEventList = it->second.moveEvent[eventType];
		if(!moveEventList.empty())
			return *moveEventList.begin();
	}

	return NULL;
}

uint32_t MoveEvents::onCreatureMove(Creature* creature, Tile* tile, bool isStepping)
{
	MoveEvent_t eventType = MOVE_EVENT_STEP_OUT;
	if(isStepping)
		eventType = MOVE_EVENT_STEP_IN;

	uint32_t ret = 1;
	MoveEvent* moveEvent = getEvent(tile, eventType);
	if(moveEvent)
		ret = ret & moveEvent->fireStepEvent(creature, NULL, tile->getPosition());

	int32_t tmp = tile->__getLastIndex();
	Item* tileItem = NULL;
	for(int32_t i = tile->__getFirstIndex(); i < tmp; ++i)
	{
		Thing* thing = tile->__getThing(i);
		if(thing && (tileItem = thing->getItem()))
		{
			moveEvent = getEvent(tileItem, eventType);
			if(moveEvent)
				ret = ret & moveEvent->fireStepEvent(creature, tileItem, tile->getPosition());
		}
	}

	return ret;
}

uint32_t MoveEvents::onPlayerEquip(Player* player, Item* item, slots_t slot, bool isCheck)
{
	if(MoveEvent* moveEvent = getEvent(item, MOVE_EVENT_EQUIP, slot))
		return moveEvent->fireEquip(player, item, slot, isCheck);

	return 1;
}

uint32_t MoveEvents::onPlayerDeEquip(Player* player, Item* item, slots_t slot, bool isRemoval)
{
	if(MoveEvent* moveEvent = getEvent(item, MOVE_EVENT_DEEQUIP, slot))
		return moveEvent->fireEquip(player, item, slot, isRemoval);

	return 1;
}

uint32_t MoveEvents::onItemMove(Creature* actor, Item* item, Tile* tile, bool isAdd)
{
	MoveEvent_t eventType1 = MOVE_EVENT_REMOVE_ITEM, eventType2 = MOVE_EVENT_REMOVE_ITEM_ITEMTILE;
	if(isAdd)
	{
		eventType1 = MOVE_EVENT_ADD_ITEM;
		eventType2 = MOVE_EVENT_ADD_ITEM_ITEMTILE;
	}

	uint32_t ret = 1;
	MoveEvent* moveEvent = getEvent(tile, eventType1);
	if(moveEvent)
		ret = ret & moveEvent->fireAddRemItem(actor, item, NULL, tile->getPosition());

	moveEvent = getEvent(item, eventType1);
	if(moveEvent)
		ret = ret & moveEvent->fireAddRemItem(actor, item, NULL, tile->getPosition());

	int32_t tmp = tile->__getLastIndex();
	Item* tileItem = NULL;
	for(int32_t i = tile->__getFirstIndex(); i < tmp; ++i)
	{
		Thing* thing = tile->__getThing(i);
		if(thing && (tileItem = thing->getItem()) && (tileItem != item))
		{
			moveEvent = getEvent(tileItem, eventType2);
			if(moveEvent)
				ret = ret & moveEvent->fireAddRemItem(actor, item, tileItem, tile->getPosition());
		}
	}

	return ret;
}

MoveEvent::MoveEvent(LuaScriptInterface* _interface):
Event(_interface)
{
	m_eventType = MOVE_EVENT_NONE;
	stepFunction = NULL;
	moveFunction = NULL;
	equipFunction = NULL;
	slot = SLOTP_WHEREEVER;
	wieldInfo = 0;
	reqLevel = 0;
	reqMagLevel = 0;
	premium = false;
}

MoveEvent::MoveEvent(const MoveEvent* copy):
Event(copy)
{
	m_eventType = copy->m_eventType;
	stepFunction = copy->stepFunction;
	moveFunction = copy->moveFunction;
	equipFunction = copy->equipFunction;
	slot = copy->slot;
	if(copy->m_eventType == MOVE_EVENT_EQUIP)
	{
		wieldInfo = copy->wieldInfo;
		reqLevel = copy->reqLevel;
		reqMagLevel = copy->reqMagLevel;
		vocationString = copy->vocationString;
		premium = copy->premium;
		vocEquipMap = copy->vocEquipMap;
	}
}

MoveEvent::~MoveEvent()
{
	//
}

std::string MoveEvent::getScriptEventName() const
{
	switch(m_eventType)
	{
		case MOVE_EVENT_STEP_IN:
			return "onStepIn";

		case MOVE_EVENT_STEP_OUT:
			return "onStepOut";

		case MOVE_EVENT_EQUIP:
			return "onEquip";

		case MOVE_EVENT_DEEQUIP:
			return "onDeEquip";

		case MOVE_EVENT_ADD_ITEM:
			return "onAddItem";

		case MOVE_EVENT_REMOVE_ITEM:
			return "onRemoveItem";

		default:
			break;
	}

	std::cout << "[Error - MoveEvent::getScriptEventName] No valid event type." << std::endl;
	return "";
}

std::string MoveEvent::getScriptEventParams() const
{
	switch(m_eventType)
	{
		case MOVE_EVENT_STEP_IN:
		case MOVE_EVENT_STEP_OUT:
			return "cid, item, position, fromPosition";

		case MOVE_EVENT_EQUIP:
		case MOVE_EVENT_DEEQUIP:
			return "cid, item, slot";

		case MOVE_EVENT_ADD_ITEM:
		case MOVE_EVENT_REMOVE_ITEM:
			return "moveItem, tileItem, position, cid";

		default:
			break;
	}

	std::cout << "[Error - MoveEvent::getScriptEventParams] No valid event type." << std::endl;
	return "";
}

bool MoveEvent::configureEvent(xmlNodePtr p)
{
	std::string strValue;
	int32_t intValue;
	if(readXMLString(p, "type", strValue) || readXMLString(p, "event", strValue))
	{
		std::string tmpStrValue = asLowerCaseString(strValue);
		if(tmpStrValue == "stepin")
			m_eventType = MOVE_EVENT_STEP_IN;
		else if(tmpStrValue == "stepout")
			m_eventType = MOVE_EVENT_STEP_OUT;
		else if(tmpStrValue == "equip")
			m_eventType = MOVE_EVENT_EQUIP;
		else if(tmpStrValue == "deequip")
			m_eventType = MOVE_EVENT_DEEQUIP;
		else if(tmpStrValue == "additem")
			m_eventType = MOVE_EVENT_ADD_ITEM;
		else if(tmpStrValue == "removeitem")
			m_eventType = MOVE_EVENT_REMOVE_ITEM;
		else
		{
			std::cout << "[Error - MoveEvent::configureMoveEvent] No valid event name \"" << strValue << "\"" << std::endl;
			return false;
		}

		if(m_eventType == MOVE_EVENT_EQUIP || m_eventType == MOVE_EVENT_DEEQUIP)
		{
			if(readXMLString(p, "slot", strValue))
			{
				std::string tmpStrValue = asLowerCaseString(strValue);
				if(tmpStrValue == "head")
					slot = SLOTP_HEAD;
				else if(tmpStrValue == "necklace")
					slot = SLOTP_NECKLACE;
				else if(tmpStrValue == "backpack")
					slot = SLOTP_BACKPACK;
				else if(tmpStrValue == "armor")
					slot = SLOTP_ARMOR;
				else if(tmpStrValue == "right-hand")
					slot = SLOTP_RIGHT;
				else if(tmpStrValue == "left-hand")
					slot = SLOTP_LEFT;
				else if(tmpStrValue == "two-handed")
					slot = SLOTP_TWO_HAND;
				else if(tmpStrValue == "hand" || tmpStrValue == "shield")
					slot = SLOTP_RIGHT | SLOTP_LEFT;
				else if(tmpStrValue == "legs")
					slot = SLOTP_LEGS;
				else if(tmpStrValue == "feet")
					slot = SLOTP_FEET;
				else if(tmpStrValue == "ring")
					slot = SLOTP_RING;
				else if(tmpStrValue == "ammo")
					slot = SLOTP_AMMO;
				else
					std::cout << "[Warning - MoveEvent::configureMoveEvent]: Unknown slot type \"" << strValue << "\"" << std::endl;
			}

			wieldInfo = 0;
			if(readXMLInteger(p, "lvl", intValue) || readXMLInteger(p, "level", intValue))
			{
	 			reqLevel = intValue;
				if(reqLevel > 0)
					wieldInfo |= WIELDINFO_LEVEL;
			}

			if(readXMLInteger(p, "maglv", intValue) || readXMLInteger(p, "maglevel", intValue))
			{
	 			reqMagLevel = intValue;
				if(reqMagLevel > 0)
					wieldInfo |= WIELDINFO_MAGLV;
			}

			if(readXMLString(p, "prem", strValue) || readXMLString(p, "premium", strValue))
			{
				premium = booleanString(strValue);
				if(premium)
					wieldInfo |= WIELDINFO_PREMIUM;
			}

			StringVec vocStringVec;
			std::string error = "";
			xmlNodePtr vocationNode = p->children;
			while(vocationNode)
			{
				if(!parseVocationNode(vocationNode, vocEquipMap, vocStringVec, error))
					std::cout << "[Warning - MoveEvent::configureEvent] " << error << std::endl;

				vocationNode = vocationNode->next;
			}

			if(!vocEquipMap.empty())
				wieldInfo |= WIELDINFO_VOCREQ;

			vocationString = parseVocationString(vocStringVec);
		}
	}
	else
	{
		std::cout << "[Error - MoveEvent::configureMoveEvent] No event found." << std::endl;
		return false;
	}

	return true;
}

bool MoveEvent::loadFunction(const std::string& functionName)
{
	std::string tmpFunctionName = asLowerCaseString(functionName);
	if(tmpFunctionName == "onstepinfield")
		stepFunction = StepInField;
	else if(tmpFunctionName == "onstepoutfield")
		stepFunction = StepOutField;
	else if(tmpFunctionName == "onaddfield")
		moveFunction = AddItemField;
	else if(tmpFunctionName == "onremovefield")
		moveFunction = RemoveItemField;
	else if(tmpFunctionName == "onequipitem")
		equipFunction = EquipItem;
	else if(tmpFunctionName == "ondeequipitem")
		equipFunction = DeEquipItem;
	else
	{
		std::cout << "[Warning - MoveEvent::loadFunction] Function \"" << functionName << "\" does not exist." << std::endl;
		return false;
	}

	m_scripted = EVENT_SCRIPT_FALSE;
	return true;
}

MoveEvent_t MoveEvent::getEventType() const
{
	if(m_eventType == MOVE_EVENT_NONE)
	{
		std::cout << "[Error - MoveEvent::getEventType] MOVE_EVENT_NONE" << std::endl;
		return (MoveEvent_t)0;
	}

	return m_eventType;
}

void MoveEvent::setEventType(MoveEvent_t type)
{
	m_eventType = type;
}

uint32_t MoveEvent::StepInField(Creature* creature, Item* item, const Position& pos)
{
	if(MagicField* field = item->getMagicField())
	{
		field->onStepInField(creature, creature->getPlayer() != NULL);
		return 1;
	}

	return LUA_ERROR_ITEM_NOT_FOUND;
}

uint32_t MoveEvent::StepOutField(Creature* creature, Item* item, const Position& pos)
{
	return 1;
}

uint32_t MoveEvent::AddItemField(Item* item, Item* tileItem, const Position& pos)
{
	if(MagicField* field = item->getMagicField())
	{
		Tile* tile = item->getTile();
		for(CreatureVector::iterator cit = tile->creatures->begin(); cit != tile->creatures->end(); ++cit)
			field->onStepInField(*cit);

		return 1;
	}

	return LUA_ERROR_ITEM_NOT_FOUND;
}

uint32_t MoveEvent::RemoveItemField(Item* item, Item* tileItem, const Position& pos)
{
	return 1;
}

uint32_t MoveEvent::EquipItem(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool isCheck)
{
	if(player->isItemAbilityEnabled(slot))
		return 1;

	if(!player->hasFlag(PlayerFlag_IgnoreEquipCheck) && moveEvent->getWieldInfo() != 0)
	{
		if(player->getLevel() < (uint32_t)moveEvent->getReqLevel() || player->getMagicLevel() < (uint32_t)moveEvent->getReqMagLv())
			return 0;

		if(moveEvent->isPremium() && !player->isPremium())
			return 0;

		if(!moveEvent->getVocEquipMap().empty() && moveEvent->getVocEquipMap().find(player->getVocationId()) == moveEvent->getVocEquipMap().end())
			return 0;
	}

	if(isCheck)
		return 1;

	const ItemType& it = Item::items[item->getID()];
	if(it.transformEquipTo != 0)
	{
		Item* newItem = g_game.transformItem(item, it.transformEquipTo);
		g_game.startDecay(newItem);
	}
	else
		player->setItemAbility(slot, true);

	if(it.abilities.invisible)
	{
		Condition* condition = Condition::createCondition((ConditionId_t)slot, CONDITION_INVISIBLE, -1, 0);
		player->addCondition(condition);
	}

	if(it.abilities.manaShield)
	{
		Condition* condition = Condition::createCondition((ConditionId_t)slot, CONDITION_MANASHIELD, -1, 0);
		player->addCondition(condition);
	}

	if(it.abilities.speed)
		g_game.changeSpeed(player, it.abilities.speed);

	if(it.abilities.conditionSuppressions)
	{
		player->setConditionSuppressions(it.abilities.conditionSuppressions, false);
		player->sendIcons();
	}

	if(it.abilities.regeneration)
	{
		Condition* condition = Condition::createCondition((ConditionId_t)slot, CONDITION_REGENERATION, -1, 0);
		if(it.abilities.healthGain)
			condition->setParam(CONDITIONPARAM_HEALTHGAIN, it.abilities.healthGain);

		if(it.abilities.healthTicks)
			condition->setParam(CONDITIONPARAM_HEALTHTICKS, it.abilities.healthTicks);

		if(it.abilities.manaGain)
			condition->setParam(CONDITIONPARAM_MANAGAIN, it.abilities.manaGain);

		if(it.abilities.manaTicks)
			condition->setParam(CONDITIONPARAM_MANATICKS, it.abilities.manaTicks);

		player->addCondition(condition);
	}

	bool needUpdateSkills = false;
	for(int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i)
	{
		if(it.abilities.skills[i])
		{
			needUpdateSkills = true;
			player->setVarSkill((skills_t)i, it.abilities.skills[i]);
		}
	}

	if(needUpdateSkills)
		player->sendSkills();

	bool needUpdateStats = false;
	for(int32_t s = STAT_FIRST; s <= STAT_LAST; ++s)
	{
		if(it.abilities.stats[s])
		{
			needUpdateStats = true;
			player->setVarStats((stats_t)s, it.abilities.stats[s]);
		}

		if(it.abilities.statsPercent[s])
		{
			needUpdateStats = true;
			player->setVarStats((stats_t)s, (int32_t)(player->getDefaultStats((stats_t)s) * ((it.abilities.statsPercent[s] - 100) / 100.f)));
		}
	}

	if(needUpdateStats)
		player->sendStats();

	return 1;
}

uint32_t MoveEvent::DeEquipItem(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool isRemoval)
{
	if(!player->isItemAbilityEnabled(slot))
		return 1;

	player->setItemAbility(slot, false);

	const ItemType& it = Item::items[item->getID()];
	if(isRemoval && it.transformDeEquipTo != 0)
	{
		g_game.transformItem(item, it.transformDeEquipTo);
		g_game.startDecay(item);
	}

	if(it.abilities.invisible)
		player->removeCondition(CONDITION_INVISIBLE, (ConditionId_t)slot);

	if(it.abilities.manaShield)
		player->removeCondition(CONDITION_MANASHIELD, (ConditionId_t)slot);

	if(it.abilities.speed != 0)
		g_game.changeSpeed(player, -it.abilities.speed);

	if(it.abilities.conditionSuppressions != 0)
	{
		player->setConditionSuppressions(it.abilities.conditionSuppressions, true);
		player->sendIcons();
	}

	if(it.abilities.regeneration)
		player->removeCondition(CONDITION_REGENERATION, (ConditionId_t)slot);

	bool needUpdateSkills = false;
	for(int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i)
	{
		if(it.abilities.skills[i] != 0)
		{
			needUpdateSkills = true;
			player->setVarSkill((skills_t)i, -it.abilities.skills[i]);
		}
	}

	if(needUpdateSkills)
		player->sendSkills();

	bool needUpdateStats = false;
	for(int32_t s = STAT_FIRST; s <= STAT_LAST; ++s)
	{
		if(it.abilities.stats[s])
		{
			needUpdateStats = true;
			player->setVarStats((stats_t)s, -it.abilities.stats[s]);
		}

		if(it.abilities.statsPercent[s])
		{
			needUpdateStats = true;
			player->setVarStats((stats_t)s, -(int32_t)(player->getDefaultStats((stats_t)s) * ((it.abilities.statsPercent[s] - 100) / 100.f)));
		}
	}

	if(needUpdateStats)
		player->sendStats();

	return 1;
}

uint32_t MoveEvent::fireStepEvent(Creature* creature, Item* item, const Position& pos)
{
	if(isScripted())
		return executeStep(creature, item, pos);

	return stepFunction(creature, item, pos);
}

uint32_t MoveEvent::executeStep(Creature* creature, Item* item, const Position& pos)
{
	//onStepIn(cid, item, position, fromPosition)
	//onStepOut(cid, item, position, fromPosition)
	if(m_scriptInterface->reserveScriptEnv())
	{
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			env->setRealPos(pos);

			std::stringstream scriptstream;
			scriptstream << "cid = " << env->addThing(creature) << std::endl;
			env->streamThing(scriptstream, "item", item, env->addThing(item));
			env->streamPosition(scriptstream, "position", pos, 0);
			env->streamPosition(scriptstream, "fromPosition", creature->getLastPosition());

			scriptstream << m_scriptData;
			int32_t result = LUA_TRUE;
			if(m_scriptInterface->loadBuffer(scriptstream.str()) != -1)
			{
				lua_State* L = m_scriptInterface->getLuaState();
				result = m_scriptInterface->getField(L, "_result");
			}

			m_scriptInterface->releaseScriptEnv();
			return (result == LUA_TRUE);
		}
		else
		{
			#ifdef __DEBUG_LUASCRIPTS__
			std::stringstream desc;
			desc << creature->getName() << " itemid: " << item->getID() << " - " << pos;
			env->setEventDesc(desc.str());
			#endif

			env->setScriptId(m_scriptId, m_scriptInterface);
			env->setRealPos(pos);

			lua_State* L = m_scriptInterface->getLuaState();
			m_scriptInterface->pushFunction(m_scriptId);

			lua_pushnumber(L, env->addThing(creature));
			LuaScriptInterface::pushThing(L, item, env->addThing(item));
			LuaScriptInterface::pushPosition(L, pos, 0);
			LuaScriptInterface::pushPosition(L, creature->getLastPosition());

			int32_t result = m_scriptInterface->callFunction(4);
			m_scriptInterface->releaseScriptEnv();

			return (result == LUA_TRUE);
		}
	}
	else
	{
		std::cout << "[Error - MoveEvent::executeStep] Call stack overflow." << std::endl;
		return 0;
	}
}

uint32_t MoveEvent::fireEquip(Player* player, Item* item, slots_t slot, bool boolean)
{
	if(isScripted())
		return executeEquip(player, item, slot);

	return equipFunction(this, player, item, slot, boolean);
}

uint32_t MoveEvent::executeEquip(Player* player, Item* item, slots_t slot)
{
	//onEquip(cid, item, slot)
	//onDeEquip(cid, item, slot)
	if(m_scriptInterface->reserveScriptEnv())
	{
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			env->setRealPos(player->getPosition());

			std::stringstream scriptstream;
			scriptstream << "cid = " << env->addThing(player) << std::endl;
			env->streamThing(scriptstream, "item", item, env->addThing(item));
			scriptstream << "slot = " << slot << std::endl;

			scriptstream << m_scriptData;
			int32_t result = LUA_TRUE;
			if(m_scriptInterface->loadBuffer(scriptstream.str()) != -1)
			{
				lua_State* L = m_scriptInterface->getLuaState();
				result = m_scriptInterface->getField(L, "_result");
			}

			m_scriptInterface->releaseScriptEnv();
			return (result == LUA_TRUE);
		}
		else
		{
			#ifdef __DEBUG_LUASCRIPTS__
			std::stringstream desc;
			desc << player->getName() << " itemid: " << item->getID() << " slot: " << slot;
			env->setEventDesc(desc.str());
			#endif

			env->setScriptId(m_scriptId, m_scriptInterface);
			env->setRealPos(player->getPosition());

			lua_State* L = m_scriptInterface->getLuaState();
			m_scriptInterface->pushFunction(m_scriptId);

			lua_pushnumber(L, env->addThing(player));
			LuaScriptInterface::pushThing(L, item, env->addThing(item));
			lua_pushnumber(L, slot);

			int32_t result = m_scriptInterface->callFunction(3);
			m_scriptInterface->releaseScriptEnv();

			return (result == LUA_TRUE);
		}
	}
	else
	{
		std::cout << "[Error - MoveEvent::executeEquip] Call stack overflow." << std::endl;
		return 0;
	}
}

uint32_t MoveEvent::fireAddRemItem(Creature* actor, Item* item, Item* tileItem, const Position& pos)
{
	if(isScripted())
		return executeAddRemItem(actor, item, tileItem, pos);

	return moveFunction(item, tileItem, pos);
}

uint32_t MoveEvent::executeAddRemItem(Creature* actor, Item* item, Item* tileItem, const Position& pos)
{
	//onAddItem(moveItem, tileItem, position, cid)
	//onRemoveItem(moveItem, tileItem, position, cid)
	if(m_scriptInterface->reserveScriptEnv())
	{
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			env->setRealPos(pos);

			std::stringstream scriptstream;
			env->streamThing(scriptstream, "moveItem", item, env->addThing(item));
			env->streamThing(scriptstream, "tileItem", tileItem, env->addThing(tileItem));
			env->streamPosition(scriptstream, "position", pos, 0);
			scriptstream << "cid = " << env->addThing(actor) << std::endl;

			scriptstream << m_scriptData;
			int32_t result = LUA_TRUE;
			if(m_scriptInterface->loadBuffer(scriptstream.str()) != -1)
			{
				lua_State* L = m_scriptInterface->getLuaState();
				result = m_scriptInterface->getField(L, "_result");
			}

			m_scriptInterface->releaseScriptEnv();
			return (result == LUA_TRUE);
		}
		else
		{
			#ifdef __DEBUG_LUASCRIPTS__
			std::stringstream desc;
			if(tileItem)
				desc << "tileid: " << tileItem->getID();

			desc << " itemid: " << item->getID() << " - " << pos;
			env->setEventDesc(desc.str());
			#endif

			env->setScriptId(m_scriptId, m_scriptInterface);
			env->setRealPos(pos);

			lua_State* L = m_scriptInterface->getLuaState();

			m_scriptInterface->pushFunction(m_scriptId);
			LuaScriptInterface::pushThing(L, item, env->addThing(item));
			LuaScriptInterface::pushThing(L, tileItem, env->addThing(tileItem));
			LuaScriptInterface::pushPosition(L, pos, 0);

			lua_pushnumber(L, env->addThing(actor));

			int32_t result = m_scriptInterface->callFunction(4);
			m_scriptInterface->releaseScriptEnv();

			return (result == LUA_TRUE);
		}
	}
	else
	{
		std::cout << "[Error - MoveEvent::executeAddRemItem] Call stack overflow." << std::endl;
		return 0;
	}
}
