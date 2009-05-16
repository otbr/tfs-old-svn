////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////
#include "otpch.h"
#include "talkaction.h"

#include "iologindata.h"
#include "ioban.h"

#include "player.h"
#include "npc.h"

#include "house.h"
#include "chat.h"
#include "teleport.h"
#include "status.h"

#ifdef __ENABLE_SERVER_DIAGNOSTIC__
#include "outputmessage.h"
#include "connection.h"
#include "admin.h"
#include "protocollogin.h"
#include "protocolold.h"
#endif

#include "configmanager.h"
#include "game.h"

extern ConfigManager g_config;
extern TalkActions* g_talkActions;
extern Game g_game;
extern Chat g_chat;

TalkActions::TalkActions() :
m_scriptInterface("TalkAction Interface")
{
	m_scriptInterface.initState();
}

TalkActions::~TalkActions()
{
	clear();
}

void TalkActions::clear()
{
	for(TalkActionsMap::iterator it = talksMap.begin(); it != talksMap.end(); ++it)
		delete it->second;

	talksMap.clear();
	m_scriptInterface.reInitState();
}

Event* TalkActions::getEvent(const std::string& nodeName)
{
	if(asLowerCaseString(nodeName) == "talkaction")
		return new TalkAction(&m_scriptInterface);

	return NULL;
}

bool TalkActions::registerEvent(Event* event, xmlNodePtr p)
{
	TalkAction* talkAction = dynamic_cast<TalkAction*>(event);
	if(!talkAction)
		return false;

	if(talksMap.find(talkAction->getWords()) != talksMap.end())
	{
		std::cout << "[Warning - TalkAction::configureEvent] Duplicate registered talkaction with words: " << talkAction->getWords() << std::endl;
		return false;
	}

	talksMap[talkAction->getWords()] = talkAction;
	return true;
}

bool TalkActions::onPlayerSay(Creature* creature, uint16_t channelId, const std::string& words, bool ignoreAccess)
{
	std::string cmdstring[TALKFILTER_LAST] = words, paramstring[TALKFILTER_LAST] = "";
	size_t loc = words.find('"', 0);
	if(loc != std::string::npos && loc >= 0)
	{
		cmdstring[TALKFILTER_QUOTATION] = std::string(words, 0, loc);
		paramstring[TALKFILTER_QUOTATION] = std::string(words, (loc + 1), (words.size() - loc - 1));
		trimString(cmdstring[TALKFILTER_QUOTATION]);
	}

	loc = words.find(" ", 0);
	if(loc != std::string::npos && loc >= 0)
	{
		cmdstring[TALKFILTER_WORD] = std::string(words, 0, loc);
		paramstring[TALKFILTER_WORD] = std::string(words, (loc + 1), (words.size() - loc - 1));
	}

	TalkAction* talkAction = NULL;
	for(TalkActionsMap::iterator it = talksMap.begin(); it != talksMap.end(); ++it)
	{
		if(it->first == cmdstring[it->second->getFilter()] || (!it->second->isSensitive() &&
			strcasecmp(it->first.c_str(), cmdstring[it->second->getFilter()].c_str()) == 0))
		{
			talkAction = it->second;
			break;
		}
	}

	if(!talkAction || (talkAction->getChannel() != -1 && talkAction->getChannel() != channelId))
		return false;

	Player* player = creature->getPlayer();
	StringVec exceptions = talkAction->getExceptions();
	if(player && ((!ignoreAccess && std::find(exceptions.begin(), exceptions.end(), asLowerCaseString(
		player->getName())) == exceptions.end() && talkAction->getAccess() > player->getAccess())
		|| player->isAccountManager()))
	{
		if(player->hasCustomFlag(PlayerCustomFlag_GamemasterPrivileges))
		{
			player->sendTextMessage(MSG_STATUS_SMALL, "You cannot execute this talkaction.");
			return true;
		}

		return false;
	}

	if(talkAction->isLogged())
	{
		if(player)
			player->sendTextMessage(MSG_STATUS_CONSOLE_RED, words.c_str());

		char buffer[100];
		sprintf(buffer, "%s.log", getFilePath(FILE_TYPE_LOG, creature->getName()).c_str());
		if(FILE* file = fopen(buffer, "a"))
		{
			fprintf(file, "[%s] %s\n", formatDate().c_str(), words.c_str());
			fclose(file);
		}
	}

	if(talkAction->isScripted())
		return talkAction->executeSay(creature, cmdstring[talkAction->getFilter()], paramstring[talkAction->getFilter()], channelId);

	if(talkAction->function)
		return talkAction->function(creature, cmdstring[talkAction->getFilter()], paramstring[talkAction->getFilter()]);

	return false;
}

TalkFunction_t TalkAction::definedFunctions[] =
{
	{"serverdiag",&serverDiag},
	{"buyhouse", &buyHouse},
 	{"sellhouse", &sellHouse},
 	{"joinguild", &joinGuild},
 	{"createguild", &createGuild},
	{"ghost", &ghost},
	{"addskill", &addSkill},
	{"changethingproporties", &changeThingProporties},
	{"showbanishmentinfo", &showBanishmentInfo}
};

TalkAction::TalkAction(LuaScriptInterface* _interface):
Event(_interface)
{
	m_filter = TALKFILTER_WORD;
	m_access = 0;
	m_channel = -1;
	m_logged = false;
	m_sensitive = true;
}

bool TalkAction::configureEvent(xmlNodePtr p)
{
	std::string strValue;
	if(readXMLString(p, "words", strValue))
		m_words = strValue;
	else
	{
		std::cout << "[Error - TalkAction::configureEvent] No words for TalkAction." << std::endl;
		return false;
	}

	if(readXMLString(p, "filter", strValue))
	{
		std::string tmpStrValue = asLowerCaseString(strValue);
		if(tmpStrValue == "quotation")
			m_filter = TALKFILTER_QUOTATION;
		else if(tmpStrValue == "word")
			m_filter = TALKFILTER_WORD;
		else
			std::cout << "[Warning - TalkAction::configureEvent] Unknown filter for TalkAction: " << strValue << ", using default." << std::endl;
	}

	int32_t intValue;
	if(readXMLInteger(p, "access", intValue))
		m_access = intValue;

	if(readXMLInteger(p, "channel", intValue))
		m_channel = intValue;

	if(readXMLString(p, "log", strValue) || readXMLString(p, "logged", strValue))
		m_logged = booleanString(strValue);

	if(readXMLString(p, "case-sensitive", strValue) || readXMLString(p, "casesensitive", strValue) || readXMLString(p, "sensitive", strValue))
		m_sensitive = booleanString(strValue);

	if(readXMLString(p, "exception", strValue))
		m_exceptions = explodeString(asLowerCaseString(strValue), ";");

	return true;
}

bool TalkAction::loadFunction(const std::string& functionName)
{
	std::string tmpFunctionName = asLowerCaseString(functionName);
	for(uint32_t i = 0; i < sizeof(definedFunctions) / sizeof(definedFunctions[0]); i++)
	{
		if(tmpFunctionName == definedFunctions[i].name)
		{
			function = definedFunctions[i].callback;
			m_scripted = EVENT_SCRIPT_FALSE;
			return true;
		}
	}

	std::cout << "[Warning - TalkAction::loadFunction] Function \"" << functionName << "\" does not exist." << std::endl;
	return false;
}

int32_t TalkAction::executeSay(Creature* creature, const std::string& words, const std::string& param, uint16_t channel)
{
	//onSay(cid, words, param, channel)
	if(m_scriptInterface->reserveScriptEnv())
	{
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			env->setRealPos(creature->getPosition());

			std::stringstream scriptstream;
			scriptstream << "cid = " << env->addThing(creature) << std::endl;
			scriptstream << "words = \"" << words << "\"" << std::endl;
			scriptstream << "param = \"" << param << "\"" << std::endl;
			scriptstream << "channel = " << channel << std::endl;

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
			char desc[125];
			sprintf(desc, "%s - %s- %s", creature->getName().c_str(), words.c_str(), param.c_str());
			env->setEventDesc(desc);
			#endif

			env->setScriptId(m_scriptId, m_scriptInterface);
			env->setRealPos(creature->getPosition());

			lua_State* L = m_scriptInterface->getLuaState();
			m_scriptInterface->pushFunction(m_scriptId);

			lua_pushnumber(L, env->addThing(creature));
			lua_pushstring(L, words.c_str());
			lua_pushstring(L, param.c_str());
			lua_pushnumber(L, channel);

			int32_t result = m_scriptInterface->callFunction(4);
			m_scriptInterface->releaseScriptEnv();

			return (result == LUA_TRUE);
		}
	}
	else
	{
		std::cout << "[Error - TalkAction::executeSay] Call stack overflow." << std::endl;
		return 0;
	}
}

bool TalkAction::serverDiag(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player)
		return false;

#ifdef __ENABLE_SERVER_DIAGNOSTIC__
	std::stringstream text;
	text << "Server diagonostic:\n";
	player->sendTextMessage(MSG_STATUS_CONSOLE_BLUE, text.str().c_str());

	text.str("");
	text << "World:" << "\n";
	text << "--------------------\n";
	text << "Player: " << g_game.getPlayersOnline() << " (" << Player::playerCount << ")\n";
	text << "Npc: " << g_game.getNpcsOnline() << " (" << Npc::npcCount << ")\n";
	text << "Monster: " << g_game.getMonstersOnline() << " (" << Monster::monsterCount << ")\n";
	player->sendTextMessage(MSG_STATUS_CONSOLE_BLUE, text.str().c_str());

	text.str("");
	text << "Protocols:" << "\n";
	text << "--------------------\n";
	text << "ProtocolGame: " << ProtocolGame::protocolGameCount << "\n";
	text << "ProtocolLogin: " << ProtocolLogin::protocolLoginCount << "\n";
#ifdef __REMOTE_CONTROL__
	text << "ProtocolAdmin: " << ProtocolAdmin::protocolAdminCount << "\n";
#endif
	text << "ProtocolStatus: " << ProtocolStatus::protocolStatusCount << "\n";
	text << "ProtocolOld: " << ProtocolOld::protocolOldCount << "\n\n";
	player->sendTextMessage(MSG_STATUS_CONSOLE_BLUE, text.str().c_str());

	text.str("");
	text << "Connections:\n";
	text << "--------------------\n";
	text << "Active connections: " << Connection::connectionCount << "\n";
	text << "Total message pool: " << OutputMessagePool::getInstance()->getTotalMessageCount() << "\n";
	text << "Auto message pool: " << OutputMessagePool::getInstance()->getAutoMessageCount() << "\n";
	text << "Free message pool: " << OutputMessagePool::getInstance()->getAvailableMessageCount() << "\n";
	player->sendTextMessage(MSG_STATUS_CONSOLE_BLUE, text.str().c_str());

	text.str("");
	text << "Libraries:\n";
	text << "--------------------\n";
	text << "asio: " << BOOST_ASIO_VERSION << "\n";
	text << "XML: " << XML_DEFAULT_VERSION << "\n";
	text << "Lua: " << LUA_VERSION << "\n";
	player->sendTextMessage(MSG_STATUS_CONSOLE_BLUE, text.str().c_str());

#endif
	return true;
}

bool TalkAction::buyHouse(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player || !g_config.getBool(ConfigManager::HOUSE_BUY_AND_SELL))
		return false;

	const Position& pos = getNextPosition(player->getDirection(), player->getPosition());
	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		player->sendCancel("You have to be looking at door of the house you would like to buy.");
		return true;
	}

	HouseTile* houseTile = tile->getHouseTile();
	if(!houseTile)
	{
		player->sendCancel("You have to be looking at door of the house you would like to buy.");
		return true;
	}

	House* house = houseTile->getHouse();
	if(!house)
	{
		player->sendCancel("You have to be looking at door of the house you would like to buy.");
		return true;
	}

	if(!house->getDoorByPosition(pos))
	{
		player->sendCancel("You have to be looking at door of the house you would like to buy.");
		return true;
	}

	if(!house->isGuild())
	{
		if(Houses::getInstance().getHouseByPlayerId(player->getGUID()))
		{
			player->sendCancel("You already own another house.");
			return true;
		}

		uint16_t accountHouses = g_config.getNumber(ConfigManager::HOUSES_PER_ACCOUNT);
		if(accountHouses > 0 && Houses::getInstance().getHousesCount(player->getAccount()) >= accountHouses)
		{
			char buffer[80];
			sprintf(buffer, "You may own only %d house%s per account.", accountHouses, (accountHouses != 1 ? "s" : ""));
			player->sendCancel(buffer);
			return true;
		}

		if(g_config.getBool(ConfigManager::HOUSE_NEED_PREMIUM) && !player->isPremium())
		{
			player->sendCancelMessage(RET_YOUNEEDPREMIUMACCOUNT);
			return true;
		}

		uint32_t levelToBuyHouse = g_config.getNumber(ConfigManager::LEVEL_TO_BUY_HOUSE);
		if(player->getLevel() < levelToBuyHouse)
		{
			char buffer[90];
			sprintf(buffer, "You have to be at least Level %d to buy a house.", levelToBuyHouse);
			player->sendCancel(buffer);
			return true;
		}
	}
	else
	{
		if(!player->getGuildId() || player->getGuildLevel() != GUILDLEVEL_LEADER)
		{
			player->sendCancel("You have to be the guild leader to purchase a guild hall.");
			return true;
		}

		if(Houses::getInstance().getHouseByGuildId(player->getGuildId()))
		{
			player->sendCancel("Your guild own another guild hall.");
			return true;
		}
	}

	if(house->getHouseOwner())
	{
		player->sendCancel("This house is already owned by someone else.");
		return true;
	}

	if(g_game.getMoney(player) < house->getPrice() || !g_game.removeMoney(player, house->getPrice()))
	{
		player->sendCancel("You do not have enough money.");
		return true;
	}

	house->setHouseOwner(player->getGUID());
	std::string ret = "You have successfully bought this house, remember to leave money at ";
	if(house->isGuild())
		ret += "guild owner ";

	if(g_config.getBool(ConfigManager::BANK_SYSTEM))
		ret += "bank or ";

	ret += "depot of this city for rent.";
	player->sendTextMessage(MSG_INFO_DESCR, ret.c_str());
	return true;
}

bool TalkAction::sellHouse(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player || !g_config.getBool(ConfigManager::HOUSE_BUY_AND_SELL))
		return false;

	House* house = Houses::getInstance().getHouseByPlayerId(player->getGUID());
	if(!house && (!player->getGuildId() || !(house = Houses::getInstance().getHouseByGuildId(player->getGuildId()))))
	{
		player->sendCancel("You do not own any house.");
		return true;
	}

	if(house->isGuild() && player->getGuildLevel() != GUILDLEVEL_LEADER)
	{
		player->sendCancel("You have to be the guild leader to sell a guild hall.");
		return false;
	}

	Player* tradePartner = NULL;
	ReturnValue ret = g_game.getPlayerByNameWildcard(param, tradePartner);
	if(ret != RET_NOERROR)
	{
		player->sendCancelMessage(ret);
		return true;
	}

	if(tradePartner == player)
	{
		player->sendCancel("You cannot trade with yourself.");
		return true;
	}

	if(!house->isGuild())
	{
		if(Houses::getInstance().getHouseByPlayerId(tradePartner->getGUID()))
		{
			player->sendCancel("Trade player already owns another house.");
			return true;
		}

		uint16_t housesPerAccount = g_config.getNumber(ConfigManager::HOUSES_PER_ACCOUNT);
		if(housesPerAccount > 0 && Houses::getInstance().getHousesCount(tradePartner->getAccount()) >= housesPerAccount)
		{
			char buffer[100];
			sprintf(buffer, "Trade player has reached limit of %d house%s per account.", housesPerAccount, (housesPerAccount != 1 ? "s" : ""));
			player->sendCancel(buffer);
			return true;
		}

		if(!tradePartner->isPremium() && !g_config.getBool(ConfigManager::HOUSE_NEED_PREMIUM))
		{
			player->sendCancel("Trade player does not have a premium account.");
			return true;
		}

		uint32_t levelToBuyHouse = g_config.getNumber(ConfigManager::LEVEL_TO_BUY_HOUSE);
		if(tradePartner->getLevel() < levelToBuyHouse)
		{
			char buffer[100];
			sprintf(buffer, "Trade player has to be at least Level %d to buy house.", levelToBuyHouse);
			player->sendCancel(buffer);
			return true;
		}
	}
	else
	{
		if(!tradePartner->getGuildId() || tradePartner->getGuildLevel() != GUILDLEVEL_LEADER)
		{
			player->sendCancel("Trade player have to be the guild leader.");
			return true;
		}

		if(Houses::getInstance().getHouseByGuildId(tradePartner->getGuildId()))
		{
			player->sendCancel("Trade player guild already own another house.");
			return true;
		}
	}

	if(!Position::areInRange<3,3,0>(tradePartner->getPosition(), player->getPosition()))
	{
		player->sendCancel("Trade player is too far away.");
		return true;
	}

	if(!Houses::getInstance().payRent(player, house))
	{
		player->sendCancel("You have to pay the rent first.");
		return true;
	}

	Item* transferItem = HouseTransferItem::createHouseTransferItem(house);
	player->transferContainer.__addThing(NULL, transferItem);

	player->transferContainer.setParent(player);
	if(!g_game.internalStartTrade(player, tradePartner, transferItem))
		transferItem->onTradeEvent(ON_TRADE_CANCEL, player, NULL);

	return true;
}

bool TalkAction::joinGuild(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player || !g_config.getBool(ConfigManager::INGAME_GUILD_MANAGEMENT))
		return false;

	std::string param_ = param;
	trimString(param_);
	if(!player->getGuildId())
	{
		uint32_t guildId;
		if(IOGuild::getInstance()->getGuildIdByName(guildId, param_))
		{
			if(player->isInvitedToGuild(guildId))
			{
				IOGuild::getInstance()->joinGuild(player, guildId);
				player->sendTextMessage(MSG_INFO_DESCR, "You have joined the guild.");

				char buffer[80];
				sprintf(buffer, "%s has joined the guild.", player->getName().c_str());
				if(ChatChannel* guildChannel = g_chat.getChannel(player, 0x00))
					guildChannel->talk(player, SPEAK_CHANNEL_R2, buffer);
			}
			else
				player->sendCancel("You are not invited to that guild.");
		}
		else
			player->sendCancel("There's no guild with that name.");
	}
	else
		player->sendCancel("You are already in a guild.");

	return true;
}

bool TalkAction::createGuild(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player || !g_config.getBool(ConfigManager::INGAME_GUILD_MANAGEMENT))
		return false;

	if(player->getGuildId())
	{
		player->sendCancel("You are already in a guild.");
		return true;
	}

	std::string param_ = param;
	trimString(param_);
	if(!isValidName(param_))
	{
		player->sendCancel("That guild name contains illegal characters, please choose another name.");
		return true;
	}

	const uint32_t minLength = g_config.getNumber(ConfigManager::MIN_GUILDNAME);
	const uint32_t maxLength = g_config.getNumber(ConfigManager::MAX_GUILDNAME);
	if(param_.length() < minLength)
	{
		player->sendCancel("That guild name is too short, please select a longer name.");
		return true;
	}

	if(param_.length() > maxLength)
	{
		player->sendCancel("That guild name is too long, please select a shorter name.");
		return true;
	}

	uint32_t guildId;
	if(IOGuild::getInstance()->getGuildIdByName(guildId, param_))
	{
		player->sendCancel("There is already a guild with that name.");
		return true;
	}

	const uint32_t levelToFormGuild = g_config.getNumber(ConfigManager::LEVEL_TO_FORM_GUILD);
	if(player->getLevel() < levelToFormGuild)
	{
		char buffer[70 + levelToFormGuild];
		sprintf(buffer, "You have to be at least Level %d to form a guild.", levelToFormGuild);
		player->sendCancel(buffer);
		return true;
	}

	const int32_t premiumDays = g_config.getNumber(ConfigManager::GUILD_PREMIUM_DAYS);
	if(player->getPremiumDays() < premiumDays)
	{
		char buffer[70 + premiumDays];
		sprintf(buffer, "You need to have at least %d premium days to form a guild.", premiumDays);
		player->sendCancel(buffer);
		return true;
	}

	player->setGuildName(param_);
	IOGuild::getInstance()->createGuild(player);

	char buffer[50 + maxLength];
	sprintf(buffer, "You have formed guild \"%s\"!", param_.c_str());
	player->sendTextMessage(MSG_INFO_DESCR, buffer);
	return true;
}

bool TalkAction::ghost(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player)
		return false;

	bool added = true;
	Condition* condition = NULL;
	if((condition = player->getCondition(CONDITION_GAMEMASTER, CONDITIONID_DEFAULT, GAMEMASTER_INVISIBLE)))
	{
		player->removeCondition(condition);
		added = false;
	}
	else if((condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_GAMEMASTER, -1, 0, false, GAMEMASTER_INVISIBLE)))
		player->addCondition(condition);

	SpectatorVec list;
	SpectatorVec::const_iterator it;
	g_game.getSpectators(list, player->getPosition(), true);

	int32_t index = player->getTopParent()->__getIndexOfThing(player);
	Player* tmpPlayer = NULL;
	for(it = list.begin(); it != list.end(); ++it)
	{
		if((tmpPlayer = (*it)->getPlayer()))
		{
			tmpPlayer->sendCreatureChangeVisible(player, !added);
			if(tmpPlayer != player && !tmpPlayer->canSeeGhost(player))
			{
				if(added)
				{
					tmpPlayer->sendCreatureDisappear(player, index, true);
					tmpPlayer->sendMagicEffect(player->getPosition(), NM_ME_POFF);
				}
				else
					tmpPlayer->sendCreatureAppear(player, player->getPosition(), index, true);

				tmpPlayer->sendUpdateTile(player->getTile(), player->getPosition());
			}
		}
	}

	for(it = list.begin(); it != list.end(); ++it)
		(*it)->onUpdateTile(player->getTile(), player->getPosition());

	if(added)
	{
		for(AutoList<Player>::listiterator it = Player::listPlayer.list.begin(); it != Player::listPlayer.list.end(); ++it)
		{
			if(!it->second->canSeeGhost(player))
				it->second->notifyLogOut(player);
		}

		Status::getInstance()->removePlayer();
		IOLoginData::getInstance()->updateOnlineStatus(player->getGUID(), false);
		player->sendTextMessage(MSG_INFO_DESCR, "You are now invisible.");
	}
	else
	{
		for(AutoList<Player>::listiterator it = Player::listPlayer.list.begin(); it != Player::listPlayer.list.end(); ++it)
		{
			if(!it->second->canSeeGhost(player))
				it->second->notifyLogIn(player);
		}

		Status::getInstance()->addPlayer();
		IOLoginData::getInstance()->updateOnlineStatus(player->getGUID(), true);
		player->sendTextMessage(MSG_INFO_DESCR, "You are visible again.");
	}

	return true;
}

bool TalkAction::addSkill(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player)
		return false;

	StringVec params = explodeString(param, ",");
	if(params.size() < 2)
	{
		player->sendTextMessage(MSG_STATUS_SMALL, "Command requires at least 2 parameters.");
		return true;
	}

	uint32_t amount = 1;
	if(params.size() > 2)
	{
		std::string tmp = params[2];
		trimString(tmp);
		amount = (uint32_t)std::max(1, atoi(tmp.c_str()));
	}

	std::string name = params[0], skill = params[1];
	trimString(name);
	trimString(skill);

	Player* target = NULL;
	ReturnValue ret = g_game.getPlayerByNameWildcard(name, target);
	if(ret != RET_NOERROR)
	{
		player->sendCancelMessage(ret);
		return true;
	}

	if(skill[0] == 'l' || skill[0] == 'e')
		target->addExperience(uint64_t(Player::getExpForLevel(target->getLevel() + amount) - target->getExperience()));
	else if(skill[0] == 'm')
		target->addManaSpent((uint64_t)std::ceil(double(target->getVocation()->getReqMana(target->getMagicLevel() +
			amount) - target->getSpentMana()) / g_config.getDouble(ConfigManager::RATE_MAGIC)), true, false);
	else
	{
		skills_t skillId = getSkillId(skill);
		target->addSkillAdvance(skillId, (uint32_t)std::ceil(double(target->getVocation()->getReqSkillTries(skillId, (target->getSkill(skillId,
			SKILL_LEVEL) + amount)) - target->getSkill(skillId, SKILL_TRIES)) / g_config.getDouble(ConfigManager::RATE_SKILL)), false);
	}

	return true;
}

bool TalkAction::changeThingProporties(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player)
		return false;

	Position playerPos = player->getPosition();
	Position pos = getNextPosition(player->getDirection(), playerPos);

	Tile* tileInFront = g_game.getTile(pos);
	if(!tileInFront)
	{
		player->sendTextMessage(MSG_STATUS_SMALL, "No tile found.");
		g_game.addMagicEffect(playerPos, NM_ME_POFF);
		return true;
	}

	Thing* thing = tileInFront->getTopThing();
	if(!thing)
	{
		player->sendTextMessage(MSG_STATUS_SMALL, "No object found.");
		g_game.addMagicEffect(playerPos, NM_ME_POFF);
		return true;
	}

	boost::char_separator<char> sep(" ");
	tokenizer cmdtokens(param, sep);

	std::string tmp;
	tokenizer::iterator cmdit = cmdtokens.begin();
	while(cmdit != cmdtokens.end())
	{
		if(Item* item = thing->getItem())
		{
			tmp = parseParams(cmdit, cmdtokens.end());
			if(strcasecmp(tmp.c_str(), "description") == 0 || strcasecmp(tmp.c_str(), "desc") == 0)
				item->setSpecialDescription(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "count") == 0 || strcasecmp(tmp.c_str(), "fluidtype") == 0 || strcasecmp(tmp.c_str(), "charges") == 0)
				item->setSubType(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "action") == 0 || strcasecmp(tmp.c_str(), "actionid") == 0 || strcasecmp(tmp.c_str(), "aid") == 0)
				item->setActionId(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "unique") == 0 || strcasecmp(tmp.c_str(), "uniqueid") == 0 || strcasecmp(tmp.c_str(), "uid") == 0)
				item->setUniqueId(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "duration") == 0)
				item->setDuration(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "writer") == 0)
				item->setWriter(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "text") == 0)
				item->setText(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "name") == 0)
				item->setName(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "pluralname") == 0)
				item->setPluralName(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "article") == 0)
				item->setArticle(parseParams(cmdit, cmdtokens.end()));
			else if(strcasecmp(tmp.c_str(), "attack") == 0)
				item->setAttack(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "extraattack") == 0)
				item->setExtraAttack(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "defense") == 0)
				item->setDefense(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "extradefense") == 0)
				item->setExtraDefense(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "armor") == 0)
				item->setArmor(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "attackspeed") == 0)
				item->setAttackSpeed(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "hitchance") == 0)
				item->setHitChance(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "shootrange") == 0)
				item->setShootRange(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "depot") == 0 || strcasecmp(tmp.c_str(), "depotid") == 0)
			{
				if(item->getContainer() && item->getContainer()->getDepot())
					item->getContainer()->getDepot()->setDepotId(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			}
			else if(strcasecmp(tmp.c_str(), "destination") == 0 || strcasecmp(tmp.c_str(), "position") == 0 || strcasecmp(tmp.c_str(), "pos") == 0 || strcasecmp(tmp.c_str(), "destpos") == 0) //FIXME
			{
				if(item->getTeleport())
					item->getTeleport()->setDestPos(Position(atoi(parseParams(cmdit, cmdtokens.end()).c_str()), atoi(parseParams(cmdit, cmdtokens.end()).c_str()), atoi(parseParams(cmdit, cmdtokens.end()).c_str())));
			}
			else
			{
				player->sendTextMessage(MSG_STATUS_SMALL, "No valid action.");
				g_game.addMagicEffect(playerPos, NM_ME_POFF);
				return true;
			}
		}
		else if(Creature* _creature = thing->getCreature())
		{
			tmp = parseParams(cmdit, cmdtokens.end());
			if(strcasecmp(tmp.c_str(), "health") == 0)
				_creature->changeHealth(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "maxhealth") == 0)
				_creature->changeMaxHealth(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "mana") == 0)
				_creature->changeMana(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "maxmana") == 0)
				_creature->changeMaxMana(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "basespeed") == 0)
				_creature->setBaseSpeed(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "droploot") == 0)
				_creature->setDropLoot(booleanString(parseParams(cmdit, cmdtokens.end()).c_str()) ? LOOT_DROP_FULL : LOOT_DROP_NONE);
			else if(strcasecmp(tmp.c_str(), "lossskill") == 0)
				_creature->setLossSkill(booleanString(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "cannotmove") == 0)
				_creature->setNoMove(booleanString(parseParams(cmdit, cmdtokens.end()).c_str()));
			else if(strcasecmp(tmp.c_str(), "skull") == 0)
			{
				_creature->setSkull((Skulls_t)atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				g_game.updateCreatureSkull(_creature);
			}
			else if(Player* _player = _creature->getPlayer())
			{
				if(strcasecmp(tmp.c_str(), "fyi") == 0)
					_player->sendFYIBox(parseParams(cmdit, cmdtokens.end()).c_str());
				else if(strcasecmp(param.c_str(), "tutorial") == 0)
					_player->sendTutorial(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "guildrank") == 0)
					_player->setGuildRankId(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "guildnick") == 0)
					_player->setGuildNick(parseParams(cmdit, cmdtokens.end()).c_str());
				else if(strcasecmp(tmp.c_str(), "group") == 0)
					_player->setGroupId(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "vocation") == 0)
					_player->setVocation(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "sex") == 0 || strcasecmp(tmp.c_str(), "gender") == 0)
					_player->setSex((PlayerSex_t)atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "stamina") == 0)
					_player->setStaminaMinutes(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "town") == 0) //FIXME
					_player->setTown(atoi(parseParams(cmdit, cmdtokens.end()).c_str()));
				else if(strcasecmp(tmp.c_str(), "balance") == 0)
					_player->balance = atoi(parseParams(cmdit, cmdtokens.end()).c_str());
				else if(strcasecmp(tmp.c_str(), "marriage") == 0)
					_player->marriage = atoi(parseParams(cmdit, cmdtokens.end()).c_str());
				else if(strcasecmp(tmp.c_str(), "rates") == 0)
					_player->rates[atoi(parseParams(cmdit, cmdtokens.end()).c_str())] = atof(
						parseParams(cmdit, cmdtokens.end()).c_str());
				else if(strcasecmp(tmp.c_str(), "resetidle") == 0)
					_player->resetIdleTime();
				else if(strcasecmp(tmp.c_str(), "saving") == 0)
					_player->switchSaving();
				else
				{
					player->sendTextMessage(MSG_STATUS_SMALL, "No valid action.");
					g_game.addMagicEffect(playerPos, NM_ME_POFF);
					return true;
				}
			}
			/*else if(Npc* _npc = _creature->getNpc())
				//
			else if(Monster* _monster = _creature->getMonster())
				//*/
			else
			{
				player->sendTextMessage(MSG_STATUS_SMALL, "No valid action.");
				g_game.addMagicEffect(playerPos, NM_ME_POFF);
				return true;
			}
		}
	}

	const Position& cylinderMapPos = tileInFront->getPosition();
	const SpectatorVec& list = g_game.getSpectators(cylinderMapPos);
	SpectatorVec::const_iterator it;

	Player* tmpPlayer = NULL;
	for(it = list.begin(); it != list.end(); ++it)
	{
		if((tmpPlayer = (*it)->getPlayer()))
			tmpPlayer->sendUpdateTile(tileInFront, cylinderMapPos);
	}

	for(it = list.begin(); it != list.end(); ++it)
		(*it)->onUpdateTile(tileInFront, cylinderMapPos);

	g_game.addMagicEffect(pos, NM_ME_MAGIC_POISON);
	return true;
}

bool TalkAction::showBanishmentInfo(Creature* creature, const std::string& cmd, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player)
		return false;

	std::string param_ = param;
	trimString(param_);

	uint32_t accountId = atoi(param_.c_str());
	if(accountId == 0 && IOLoginData::getInstance()->playerExists(param_, true))
		accountId = IOLoginData::getInstance()->getAccountIdByName(param_);

	Ban ban;
	if(IOBan::getInstance()->getData(accountId, ban) && (ban.type == BANTYPE_BANISHMENT || ban.type == BANTYPE_DELETION))
	{
		bool deletion = (ban.type == BANTYPE_DELETION);
		std::string name = "Automatic ";
		if(ban.adminid == 0)
			name += (deletion ? "deletion" : "banishment");
		else
			IOLoginData::getInstance()->getNameByGuid(ban.adminid, name, true);

		char buffer[500 + ban.comment.length()];
		sprintf(buffer, "Account has been %s at:\n%s by: %s,\nfor the following reason:\n%s.\nThe action taken was:\n%s.\nThe comment given was:\n%s.\n%s%s.",
			(deletion ? "deleted" : "banished"), formatDateShort(ban.added).c_str(), name.c_str(), getReason(ban.reason).c_str(),
			getAction(ban.action, false).c_str(), ban.comment.c_str(),
			(deletion ? "Account won't be undeleted" : "Banishment will be lifted at:\n"),
			(deletion ? "." : formatDateShort(ban.expires, true).c_str()));

		player->sendFYIBox(buffer);
	}
	else
		player->sendCancel("That player or account is not banished or deleted.");

	return true;
}
